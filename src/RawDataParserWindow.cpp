#include "AppTheme.h"
#include "RawDataParserWindow.h"
#include "CustomTitleBar.h"
#include "TcpWaveEncoding.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableView>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QTimeZone>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringConverter>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::appThemeColorName;
using VaporView::configureComboBoxPopup;
using VaporView::isDarkThemeEnabled;

namespace
{
void applyRawDataProgressDialogStyle(QProgressDialog *dialog)
{
    if (!dialog)
    {
        return;
    }

    const bool dark = isDarkThemeEnabled();
    const QColor panelColor = appThemeColor(dark ? AppThemeColor::Window : AppThemeColor::Surface, dark);
    const QColor fieldColor = appThemeColor(AppThemeColor::FieldBackground, dark);
    const QColor borderColor = appThemeColor(AppThemeColor::FieldBorder, dark);
    const QColor textColor = appThemeColor(AppThemeColor::TextStrong, dark);
    const QColor chunkColor = appThemeColor(AppThemeColor::ProgressChunk, dark);

    QPalette progressPalette = dialog->palette();
    progressPalette.setColor(QPalette::Window, panelColor);
    progressPalette.setColor(QPalette::Base, panelColor);
    progressPalette.setColor(QPalette::Text, textColor);
    progressPalette.setColor(QPalette::WindowText, textColor);
    dialog->setPalette(progressPalette);
    dialog->setAttribute(Qt::WA_StyledBackground, true);
    dialog->setAutoFillBackground(true);

    if (QWidget *content = dialog->findChild<QWidget *>(QStringLiteral("customTitleBarContent")))
    {
        content->setAutoFillBackground(true);
        content->setPalette(progressPalette);
        if (auto *layout = qobject_cast<QVBoxLayout *>(content->layout()))
        {
            layout->setContentsMargins(22, 18, 22, 18);
            layout->setSpacing(14);
        }
    }

    dialog->setStyleSheet(QStringLiteral(
        "QProgressDialog, QWidget#customTitleBarContent { background-color: %1; color: %2; }"
        "QWidget#customTitleBarContent QLabel { background-color: transparent; color: %2; font-size: 14px; }"
        "QWidget#customTitleBarContent QProgressBar { background-color: %3; border: 1px solid %4; border-radius: 4px; min-height: 10px; text-align: center; color: %2; }"
        "QWidget#customTitleBarContent QProgressBar::chunk { background-color: %5; border-radius: 3px; }"
        "QWidget#customTitleBarContent QPushButton { background-color: %6; color: %7; border: none; border-radius: 6px; min-height: 34px; padding: 0px 16px 2px 16px; }"
        "QWidget#customTitleBarContent QPushButton:hover { background-color: %8; }"
        "QWidget#customTitleBarContent QPushButton:pressed { background-color: %9; }")
        .arg(panelColor.name(),
             textColor.name(),
             fieldColor.name(),
             borderColor.name(),
             chunkColor.name(),
             appThemeColorName(AppThemeColor::Primary, dark),
             appThemeColorName(AppThemeColor::White, dark),
             appThemeColorName(AppThemeColor::PrimaryHover, dark),
             appThemeColorName(AppThemeColor::PrimaryPressed, dark)));
}

void applyRawDataInlineProgressStyle(QWidget *panel, QLabel *label, QProgressBar *bar)
{
    if (!panel || !label || !bar)
    {
        return;
    }

    const bool dark = isDarkThemeEnabled();
    const QColor panelColor = appThemeColor(dark ? AppThemeColor::Window : AppThemeColor::Surface, dark);
    const QColor fieldColor = appThemeColor(AppThemeColor::FieldBackground, dark);
    const QColor borderColor = appThemeColor(AppThemeColor::FieldBorder, dark);
    const QColor textColor = appThemeColor(AppThemeColor::TextStrong, dark);
    const QColor chunkColor = appThemeColor(AppThemeColor::ProgressChunk, dark);

    panel->setStyleSheet(QStringLiteral(
        "QWidget#rawDataParserProgressPanel { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
        "QLabel#rawDataParserProgressLabel { background-color: transparent; color: %3; font-size: 13px; }"
        "QProgressBar#rawDataParserProgressBar { background-color: %4; border: 1px solid %2; border-radius: 4px; min-height: 12px; text-align: center; color: %3; }"
        "QProgressBar#rawDataParserProgressBar::chunk { background-color: %5; border-radius: 3px; }")
        .arg(panelColor.name(),
             borderColor.name(),
             textColor.name(),
             fieldColor.name(),
             chunkColor.name()));
}

constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
constexpr int kMaxHexBytes = 16 * 1024;
constexpr int kRawDataParserDefaultWidth = 1280;
constexpr int kRawDataParserDefaultHeight = 800;
constexpr int kFloatBytes = 4;
constexpr uint8_t kFdilinkFrameHead = 0xFC;
constexpr uint8_t kFdilinkFrameTail = 0xFD;
constexpr uint8_t kHmpSlaveAddress = 240;

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

struct RawRecordIndex
{
    QString filename;
    QString device_name;
    quint16 source_id = 0;
    quint16 record_type = 0;
    quint32 flags = 0;
    quint64 sequence = 0;
    quint64 host_timestamp_us = 0;
    quint32 payload_size = 0;
    quint64 record_offset = 0;
    quint64 payload_offset = 0;
};

struct RawFileSummary
{
    QString device_name;
    QString filename;
    qint64 file_size = 0;
    quint64 record_count = 0;
    quint64 first_timestamp_us = 0;
    quint64 last_timestamp_us = 0;
    QString status;
};

struct RawScanResult
{
    QString session_directory;
    QVector<RawRecordIndex> records;
    QVector<RawFileSummary> file_summaries;
};

struct RawScanProgress
{
    std::atomic<qint64> total_bytes{0};
    std::atomic<qint64> scanned_bytes{0};
    std::atomic<qint64> indexed_records{0};
    std::atomic<int> total_files{0};
    std::atomic<int> completed_files{0};
    std::atomic_bool cancelled{false};
};

struct RawDecodedField
{
    QString group;
    QString name;
    QString raw_value;
    QString value;
    QString unit;
    int offset = -1;
    int length = 0;
    QString note;
    bool abnormal = false;
};

struct RawDecodedRecord
{
    bool ok = true;
    QString title;
    QString status;
    QString summary;
    QVector<RawDecodedField> fields;
};

QString csvEscape(const QString& value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString formatHex(quint64 value, int width = 2)
{
    return QStringLiteral("0x%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString formatTimestamp(quint64 timestampUs)
{
    if (timestampUs == 0)
    {
        return QStringLiteral("---");
    }
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000ULL), QTimeZone::UTC);
    return QStringLiteral("%1.%2Z")
        .arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
        .arg(static_cast<int>(timestampUs % 1000ULL), 3, 10, QLatin1Char('0'));
}

QString formatByteCount(qint64 bytes)
{
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1.0)
    {
        return QStringLiteral("%1 MB").arg(mib, 0, 'f', mib >= 100.0 ? 0 : 1);
    }

    const double kib = static_cast<double>(bytes) / 1024.0;
    if (kib >= 1.0)
    {
        return QStringLiteral("%1 KB").arg(kib, 0, 'f', kib >= 100.0 ? 0 : 1);
    }

    return QStringLiteral("%1 B").arg(bytes);
}

QString sourceName(quint16 sourceId, bool english)
{
    switch (sourceId)
    {
    case kRawSourceEpsilon:
        return english ? QStringLiteral("EPSILON") : QStringLiteral("EPSILON组合导航");
    case kRawSourcePtb:
        return english ? QStringLiteral("PTB210") : QStringLiteral("PTB210气压计");
    case kRawSourceHmp:
        return english ? QStringLiteral("HMP3") : QStringLiteral("HMP3温湿度");
    case kRawSourceLidar:
        return english ? QStringLiteral("Lidar") : QStringLiteral("激光测距");
    case kRawSourceTcpWave:
        return english ? QStringLiteral("TCP Wave") : QStringLiteral("TCP波形");
    default:
        return QStringLiteral("source %1").arg(sourceId);
    }
}

QString epsilonPacketName(quint16 recordType)
{
    switch (recordType)
    {
    case 0x40: return QStringLiteral("0x40 IMU");
    case 0x41: return QStringLiteral("0x41 AHRS");
    case 0x42: return QStringLiteral("0x42 INS/GPS");
    case 0x50: return QStringLiteral("0x50 SYS_STATE");
    case 0x51: return QStringLiteral("0x51 UNIX_TIME");
    case 0x52: return QStringLiteral("0x52 FORMATTED_TIME");
    case 0x59: return QStringLiteral("0x59 RAW_GNSS");
    case 0x5A: return QStringLiteral("0x5A SATELLITES");
    case 0x5C: return QStringLiteral("0x5C GEODETIC_POS");
    case 0x5D: return QStringLiteral("0x5D ECEF_POS");
    case 0xF0: return QStringLiteral("0xF0 MAVLink Tunnel");
    default: return QStringLiteral("%1 Unknown").arg(formatHex(recordType));
    }
}

QString lidarProtocolName(quint16 recordType, bool english)
{
    switch (recordType)
    {
    case 2: return QStringLiteral("TFA1500 Distance");
    case 3: return english ? QStringLiteral("TFA1500 Low Frequency") : QStringLiteral("TFA1500低频");
    case 4: return english ? QStringLiteral("TFA1500 High Frequency") : QStringLiteral("TFA1500高频");
    case 5: return QStringLiteral("AA-B7");
    default: return english ? QStringLiteral("Unknown Lidar") : QStringLiteral("未知激光协议");
    }
}

QString recordTypeName(quint16 sourceId, quint16 recordType, bool english)
{
    switch (sourceId)
    {
    case kRawSourceEpsilon:
        return epsilonPacketName(recordType);
    case kRawSourcePtb:
        return QStringLiteral("PTB response");
    case kRawSourceHmp:
        return QStringLiteral("Modbus function %1").arg(formatHex(recordType));
    case kRawSourceLidar:
        return lidarProtocolName(recordType, english);
    case kRawSourceTcpWave:
        return QStringLiteral("TCP wave payload");
    default:
        return QStringLiteral("record_type %1").arg(recordType);
    }
}

void scanRawFileIndex(const QString& filename,
                      quint16 expectedSourceId,
                      bool english,
                      RawScanResult& result,
                      const std::shared_ptr<RawScanProgress>& progress)
{
    RawFileSummary summary;
    summary.device_name = sourceName(expectedSourceId, english);
    summary.filename = filename;

    const qint64 progressBase = progress ? progress->scanned_bytes.load(std::memory_order_relaxed) : 0;
    auto finishFileProgress = [&]() {
        if (progress && summary.file_size > 0)
        {
            progress->scanned_bytes.store(progressBase + summary.file_size, std::memory_order_relaxed);
            progress->indexed_records.store(result.records.size(), std::memory_order_relaxed);
        }
    };
    auto isCancelled = [&]() {
        return progress && progress->cancelled.load(std::memory_order_relaxed);
    };

    QFileInfo info(filename);
    if (!info.exists())
    {
        summary.status = english ? QStringLiteral("Missing") : QStringLiteral("缺失");
        result.file_summaries.push_back(summary);
        return;
    }
    summary.file_size = info.size();
    if (isCancelled())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
    {
        summary.status = english ? QStringLiteral("Open failed") : QStringLiteral("打开失败");
        result.file_summaries.push_back(summary);
        finishFileProgress();
        return;
    }

    UnifiedRawFileHeader fileHeader{};
    if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) != static_cast<qint64>(sizeof(fileHeader)) ||
        std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) != 0)
    {
        summary.status = english ? QStringLiteral("Invalid header") : QStringLiteral("文件头无效");
        result.file_summaries.push_back(summary);
        finishFileProgress();
        return;
    }

    const quint32 fileHeaderSize = qFromLittleEndian(fileHeader.header_size);
    const quint16 sourceId = qFromLittleEndian(fileHeader.source_id);
    if (fileHeaderSize < sizeof(UnifiedRawFileHeader) || !file.seek(fileHeaderSize))
    {
        summary.status = english ? QStringLiteral("Invalid header size") : QStringLiteral("文件头长度无效");
        result.file_summaries.push_back(summary);
        finishFileProgress();
        return;
    }
    if (sourceId != expectedSourceId)
    {
        summary.status = (english
            ? QStringLiteral("Unexpected source %1")
            : QStringLiteral("数据源不匹配 %1")).arg(sourceId);
    }
    else
    {
        summary.status = english ? QStringLiteral("OK") : QStringLiteral("正常");
    }

    while (!file.atEnd())
    {
        if (isCancelled())
        {
            summary.status = english ? QStringLiteral("Canceled") : QStringLiteral("已取消");
            result.file_summaries.push_back(summary);
            finishFileProgress();
            return;
        }

        const qint64 recordOffset = file.pos();
        UnifiedRawRecordHeader header{};
        const qint64 headerBytes = file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (headerBytes == 0)
        {
            break;
        }
        if (headerBytes != static_cast<qint64>(sizeof(header)))
        {
            summary.status = english ? QStringLiteral("Incomplete record header") : QStringLiteral("记录头不完整");
            break;
        }

        const quint32 marker = qFromLittleEndian(header.marker);
        const quint32 recordHeaderSize = qFromLittleEndian(header.header_size);
        const quint32 payloadSize = qFromLittleEndian(header.payload_size);
        if (marker != kUnifiedRawRecordMarker || recordHeaderSize < sizeof(UnifiedRawRecordHeader))
        {
            summary.status = english ? QStringLiteral("Invalid record marker") : QStringLiteral("记录标记无效");
            break;
        }

        const qint64 payloadOffset = recordOffset + static_cast<qint64>(recordHeaderSize);
        const qint64 nextRecord = payloadOffset + static_cast<qint64>(payloadSize);
        if (nextRecord > file.size())
        {
            summary.status = english ? QStringLiteral("Incomplete payload") : QStringLiteral("payload 不完整");
            break;
        }

        RawRecordIndex record;
        record.filename = filename;
        record.source_id = qFromLittleEndian(header.source_id);
        record.record_type = qFromLittleEndian(header.record_type);
        record.flags = qFromLittleEndian(header.flags);
        record.sequence = qFromLittleEndian(header.sequence);
        record.host_timestamp_us = qFromLittleEndian(header.host_timestamp_us);
        record.payload_size = payloadSize;
        record.record_offset = static_cast<quint64>(recordOffset);
        record.payload_offset = static_cast<quint64>(payloadOffset);
        record.device_name = sourceName(record.source_id, english);
        result.records.push_back(record);

        ++summary.record_count;
        if (progress && (summary.record_count % 512ULL == 0ULL))
        {
            const qint64 scannedInFile = std::min(file.pos(), summary.file_size);
            progress->scanned_bytes.store(progressBase + scannedInFile, std::memory_order_relaxed);
            progress->indexed_records.store(result.records.size(), std::memory_order_relaxed);
        }
        if (summary.first_timestamp_us == 0)
        {
            summary.first_timestamp_us = record.host_timestamp_us;
        }
        summary.last_timestamp_us = record.host_timestamp_us;

        if (!file.seek(nextRecord))
        {
            break;
        }
    }

    result.file_summaries.push_back(summary);
    finishFileProgress();
}

RawScanResult scanRawSession(const QString& sessionDirectory,
                             bool english,
                             const std::shared_ptr<RawScanProgress>& progress)
{
    RawScanResult result;
    result.session_directory = sessionDirectory;

    const QDir rawDir(QDir(sessionDirectory).filePath(QStringLiteral("raw")));
    const QVector<QPair<QString, quint16>> files = {
        {QStringLiteral("epsilon.dat"), kRawSourceEpsilon},
        {QStringLiteral("ptb.dat"), kRawSourcePtb},
        {QStringLiteral("hmp.dat"), kRawSourceHmp},
        {QStringLiteral("lidar.dat"), kRawSourceLidar},
        {QStringLiteral("tcp_wave.dat"), kRawSourceTcpWave},
    };

    if (progress)
    {
        qint64 totalBytes = 0;
        for (const auto& file : files)
        {
            const QFileInfo info(rawDir.filePath(file.first));
            if (info.exists())
            {
                totalBytes += info.size();
            }
        }
        progress->total_files.store(files.size(), std::memory_order_relaxed);
        progress->total_bytes.store(totalBytes, std::memory_order_relaxed);
        progress->scanned_bytes.store(0, std::memory_order_relaxed);
        progress->indexed_records.store(0, std::memory_order_relaxed);
        progress->completed_files.store(0, std::memory_order_relaxed);
    }

    for (int i = 0; i < files.size(); ++i)
    {
        if (progress && progress->cancelled.load(std::memory_order_relaxed))
        {
            break;
        }
        scanRawFileIndex(rawDir.filePath(files.at(i).first), files.at(i).second, english, result, progress);
        if (progress)
        {
            progress->completed_files.store(i + 1, std::memory_order_relaxed);
        }
    }

    return result;
}

uint16_t readU16LE(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32LE(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

int32_t readI32LE(const uint8_t* data)
{
    return static_cast<int32_t>(readU32LE(data));
}

uint64_t readU64LE(const uint8_t* data)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

int64_t readI64LE(const uint8_t* data)
{
    return static_cast<int64_t>(readU64LE(data));
}

float readFloatLE(const uint8_t* data)
{
    float value = 0.0f;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

double readDoubleLE(const uint8_t* data)
{
    double value = 0.0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

double radToDeg(double radians)
{
    return radians * 180.0 / 3.14159265358979323846;
}

uint8_t fdilinkCrc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x01u) ? static_cast<uint8_t>((crc >> 1) ^ 0x8Cu) : static_cast<uint8_t>(crc >> 1);
        }
    }
    return crc;
}

uint16_t fdilinkCrc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

uint16_t modbusCrc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x0001u) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001u) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

float decodeModbusFloatLE(uint16_t reg0, uint16_t reg1)
{
    const uint32_t bits = static_cast<uint32_t>(reg0) | (static_cast<uint32_t>(reg1) << 16);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

QString number(double value, int decimals = 6)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }
    return QString::number(value, 'f', decimals);
}

void addField(RawDecodedRecord& decoded,
              const QString& group,
              const QString& name,
              const QString& rawValue,
              const QString& value = QString(),
              const QString& unit = QString(),
              int offset = -1,
              int length = 0,
              const QString& note = QString(),
              bool abnormal = false)
{
    RawDecodedField field;
    field.group = group;
    field.name = name;
    field.raw_value = rawValue;
    field.value = value;
    field.unit = unit;
    field.offset = offset;
    field.length = length;
    field.note = note;
    field.abnormal = abnormal;
    decoded.fields.push_back(field);
    if (abnormal)
    {
        decoded.ok = false;
    }
}

void addNumericField(RawDecodedRecord& decoded,
                     const QString& group,
                     const QString& name,
                     double value,
                     const QString& unit,
                     int offset,
                     int length,
                     int decimals = 6)
{
    addField(decoded, group, name, QString::number(value, 'g', 10), number(value, decimals), unit, offset, length);
}

QString hexPreview(const QByteArray& data, int maxBytes = 64)
{
    QStringList parts;
    const int count = std::min<int>(static_cast<int>(data.size()), maxBytes);
    parts.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        parts << QStringLiteral("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    QString text = parts.join(QLatin1Char(' '));
    if (data.size() > maxBytes)
    {
        text += QStringLiteral(" ...");
    }
    return text;
}

class RawRecordModel : public QAbstractTableModel
{
public:
    explicit RawRecordModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    void setSource(const QVector<RawRecordIndex> *records, const QVector<int> *visibleRows)
    {
        beginResetModel();
        records_ = records;
        visible_rows_ = visibleRows;
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid() || !visible_rows_)
        {
            return 0;
        }
        return visible_rows_->size();
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 8;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        {
            return QVariant();
        }
        static const QStringList headers = {
            QStringLiteral("#"),
            QStringLiteral("Time"),
            QStringLiteral("Device"),
            QStringLiteral("Type"),
            QStringLiteral("Bytes"),
            QStringLiteral("Seq"),
            QStringLiteral("Status"),
            QStringLiteral("Summary"),
        };
        return headers.value(section);
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || !records_ || !visible_rows_ ||
            index.row() < 0 || index.row() >= visible_rows_->size())
        {
            return QVariant();
        }
        const RawRecordIndex& record = records_->at(visible_rows_->at(index.row()));
        if (role == Qt::UserRole)
        {
            return visible_rows_->at(index.row());
        }
        if (role != Qt::DisplayRole)
        {
            return QVariant();
        }

        switch (index.column())
        {
        case 0:
            return index.row() + 1;
        case 1:
            return formatTimestamp(record.host_timestamp_us);
        case 2:
            return record.device_name;
        case 3:
            return recordTypeName(record.source_id, record.record_type, false);
        case 4:
            return record.payload_size;
        case 5:
            return QString::number(record.sequence);
        case 6:
            return QStringLiteral("Indexed");
        case 7:
            return QStringLiteral("%1, flags=%2").arg(recordTypeName(record.source_id, record.record_type, false), formatHex(record.flags, 8));
        default:
            return QVariant();
        }
    }

private:
    const QVector<RawRecordIndex> *records_ = nullptr;
    const QVector<int> *visible_rows_ = nullptr;
};

RawDecodedRecord decodeRawRecord(const RawRecordIndex& record, const QByteArray& payload, bool english);

}  // namespace

struct RawDataParserWindow::Impl
{
    explicit Impl(RawDataParserWindow *owner)
        : owner(owner)
    {
    }

    RawDataParserWindow *owner = nullptr;
    bool english = false;
    QString session_directory;
    QVector<RawRecordIndex> records;
    QVector<int> visible_rows;
    QVector<RawFileSummary> file_summaries;
    QHash<QString, RawDecodedRecord> decode_cache;
    QByteArray current_payload;
    QVector<QPair<int, int>> current_hex_positions;
    QFutureWatcher<RawScanResult> *scan_watcher = nullptr;
    std::shared_ptr<RawScanProgress> scan_progress;

    QWidget *central = nullptr;
    QWidget *scan_progress_panel = nullptr;
    QLabel *scan_progress_label = nullptr;
    QProgressBar *scan_progress_bar = nullptr;
    QTimer *scan_progress_timer = nullptr;
    QLabel *status_label = nullptr;
    QListWidget *file_list = nullptr;
    QComboBox *device_combo = nullptr;
    QLineEdit *type_filter = nullptr;
    QLineEdit *time_from = nullptr;
    QLineEdit *time_to = nullptr;
    QLineEdit *seq_from = nullptr;
    QLineEdit *seq_to = nullptr;
    QLineEdit *payload_min = nullptr;
    QLineEdit *payload_max = nullptr;
    QLineEdit *search_edit = nullptr;
    QCheckBox *abnormal_only = nullptr;
    QPushButton *reload_btn = nullptr;
    QPushButton *export_csv_btn = nullptr;
    QPushButton *export_json_btn = nullptr;
    QPushButton *export_bin_btn = nullptr;
    QPushButton *export_decoded_csv_btn = nullptr;
    QPushButton *export_decoded_json_btn = nullptr;
    QTableView *record_table = nullptr;
    RawRecordModel *record_model = nullptr;
    QTreeWidget *detail_tree = nullptr;
    QPlainTextEdit *hex_view = nullptr;

    void setupUi();
    void shutdown();
    void setEnglish(bool value);
    bool openSessionPath(const QString& path);
    QString resolveSessionDirectory(const QString& path) const;
    void scanSession();
    void scanRawFile(const QString& filename, quint16 expectedSourceId);
    void finishScanSession();
    void setScanControlsEnabled(bool enabled);
    void updateScanProgress();
    void refreshFileList();
    void refreshDeviceFilter();
    void applyFilters();
    bool recordMatches(const RawRecordIndex& record, const QString& searchLower, bool requireDecoded) const;
    QByteArray readPayload(const RawRecordIndex& record) const;
    RawDecodedRecord decodeRecord(const RawRecordIndex& record);
    QString cacheKey(const RawRecordIndex& record) const;
    void showSelectedRecord();
    void showDecodedRecord(const RawRecordIndex& record, const QByteArray& payload, const RawDecodedRecord& decoded);
    void setHexPayload(const QByteArray& payload);
    void highlightHexRange(int offset, int length);
    void exportFilteredCsv();
    void exportSelectedJson();
    void exportSelectedPayload();
    void exportDecodedCsv();
    void exportDecodedJson();
    QJsonObject decodedRecordToJson(const RawRecordIndex& record, const RawDecodedRecord& decoded) const;
    int currentRecordIndex() const;
    int selectedSourceId() const;
    bool parseFilterNumber(const QLineEdit *edit, quint64& value) const;
    bool parseTypeFilter(quint16& value) const;
};

RawDataParserWindow::RawDataParserWindow(QWidget *parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(this))
{
    setWindowFlag(Qt::Window, true);
    impl_->setupUi();
    VaporView::installCustomTitleBar(this);
    impl_->setEnglish(false);
}

RawDataParserWindow::~RawDataParserWindow()
{
    impl_->shutdown();
}

void RawDataParserWindow::setEnglish(bool english)
{
    impl_->setEnglish(english);
}

bool RawDataParserWindow::openSessionPath(const QString& path)
{
    return impl_->openSessionPath(path);
}

void RawDataParserWindow::Impl::setupUi()
{
    owner->resize(kRawDataParserDefaultWidth, kRawDataParserDefaultHeight);
    central = new QWidget(owner);
    owner->setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto *filterGroup = new QGroupBox(owner);
    filterGroup->setObjectName(QStringLiteral("sensorGroupBox"));
    auto *filterLayout = new QVBoxLayout(filterGroup);
    filterLayout->setContentsMargins(8, 24, 8, 8);
    filterLayout->setSpacing(6);

    device_combo = new QComboBox(owner);
    configureComboBoxPopup(device_combo, isDarkThemeEnabled());
    type_filter = new QLineEdit(owner);
    time_from = new QLineEdit(owner);
    time_to = new QLineEdit(owner);
    seq_from = new QLineEdit(owner);
    seq_to = new QLineEdit(owner);
    payload_min = new QLineEdit(owner);
    payload_max = new QLineEdit(owner);
    search_edit = new QLineEdit(owner);
    abnormal_only = new QCheckBox(owner);
    reload_btn = new QPushButton(owner);
    export_csv_btn = new QPushButton(owner);
    export_json_btn = new QPushButton(owner);
    export_bin_btn = new QPushButton(owner);
    export_decoded_csv_btn = new QPushButton(owner);
    export_decoded_json_btn = new QPushButton(owner);

    device_combo->setMinimumWidth(170);
    type_filter->setFixedWidth(170);
    time_from->setFixedWidth(170);
    time_to->setFixedWidth(170);
    seq_from->setFixedWidth(170);
    seq_to->setFixedWidth(170);
    payload_min->setFixedWidth(170);
    payload_max->setFixedWidth(170);
    search_edit->setMinimumWidth(170);

    auto makePair = [this](const QString& labelText, QWidget *control) {
        auto *container = new QWidget(owner);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto *label = new QLabel(labelText, container);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        layout->addWidget(label);
        layout->addWidget(control);
        return container;
    };
    auto makeRangePair = [this](const QString& labelText, QWidget *from, QWidget *to) {
        auto *container = new QWidget(owner);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto *label = new QLabel(labelText, container);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        layout->addWidget(label);
        layout->addWidget(from);
        layout->addWidget(to);
        return container;
    };

    auto *filterRow1 = new QHBoxLayout();
    filterRow1->setContentsMargins(0, 0, 0, 0);
    filterRow1->setSpacing(8);
    filterRow1->addWidget(makePair(QStringLiteral("Device"), device_combo), 0);
    filterRow1->addWidget(makePair(QStringLiteral("Type"), type_filter), 0);
    filterRow1->addWidget(makeRangePair(QStringLiteral("Time us"), time_from, time_to), 0);
    filterRow1->addStretch(1);
    filterRow1->addWidget(abnormal_only, 0);
    filterRow1->addWidget(reload_btn, 0);
    filterLayout->addLayout(filterRow1);

    auto *filterRow2 = new QHBoxLayout();
    filterRow2->setContentsMargins(0, 0, 0, 0);
    filterRow2->setSpacing(8);
    filterRow2->addWidget(makeRangePair(QStringLiteral("Seq"), seq_from, seq_to), 0);
    filterRow2->addWidget(makeRangePair(QStringLiteral("Payload"), payload_min, payload_max), 0);
    filterRow2->addWidget(search_edit, 1);
    filterRow2->addWidget(export_csv_btn, 0);
    filterRow2->addWidget(export_json_btn, 0);
    filterRow2->addWidget(export_bin_btn, 0);
    filterLayout->addLayout(filterRow2);

    auto *filterRow3 = new QHBoxLayout();
    filterRow3->setContentsMargins(0, 0, 0, 0);
    filterRow3->setSpacing(8);
    filterRow3->addStretch(1);
    filterRow3->addWidget(export_decoded_csv_btn, 0);
    filterRow3->addWidget(export_decoded_json_btn, 0);
    filterLayout->addLayout(filterRow3);
    mainLayout->addWidget(filterGroup);

    scan_progress_panel = new QWidget(owner);
    scan_progress_panel->setObjectName(QStringLiteral("rawDataParserProgressPanel"));
    auto *scanProgressLayout = new QVBoxLayout(scan_progress_panel);
    scanProgressLayout->setContentsMargins(10, 8, 10, 8);
    scanProgressLayout->setSpacing(6);
    scan_progress_label = new QLabel(scan_progress_panel);
    scan_progress_label->setObjectName(QStringLiteral("rawDataParserProgressLabel"));
    scan_progress_label->setWordWrap(true);
    scan_progress_bar = new QProgressBar(scan_progress_panel);
    scan_progress_bar->setObjectName(QStringLiteral("rawDataParserProgressBar"));
    scan_progress_bar->setRange(0, 1000);
    scan_progress_bar->setValue(0);
    scan_progress_bar->setTextVisible(true);
    scanProgressLayout->addWidget(scan_progress_label);
    scanProgressLayout->addWidget(scan_progress_bar);
    applyRawDataInlineProgressStyle(scan_progress_panel, scan_progress_label, scan_progress_bar);
    scan_progress_panel->setVisible(false);
    mainLayout->addWidget(scan_progress_panel);

    auto *splitter = new QSplitter(Qt::Horizontal, owner);
    file_list = new QListWidget(owner);
    file_list->setMinimumWidth(280);
    splitter->addWidget(file_list);

    record_model = new RawRecordModel(owner);
    record_table = new QTableView(owner);
    record_table->setModel(record_model);
    record_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    record_table->setSelectionMode(QAbstractItemView::SingleSelection);
    record_table->setSortingEnabled(false);
    record_table->verticalHeader()->setVisible(false);
    record_table->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(record_table);

    auto *detailSplitter = new QSplitter(Qt::Vertical, owner);
    detail_tree = new QTreeWidget(owner);
    detail_tree->setColumnCount(6);
    detail_tree->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Raw"), QStringLiteral("Value"), QStringLiteral("Unit"), QStringLiteral("Offset"), QStringLiteral("Note")});
    detail_tree->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detail_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    detail_tree->header()->setStretchLastSection(true);
    detail_tree->setColumnWidth(0, 180);
    detail_tree->setColumnWidth(1, 150);
    detail_tree->setColumnWidth(2, 210);
    detail_tree->setColumnWidth(3, 80);
    detail_tree->setColumnWidth(4, 90);
    detailSplitter->addWidget(detail_tree);

    hex_view = new QPlainTextEdit(owner);
    hex_view->setReadOnly(true);
    hex_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    hex_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detailSplitter->addWidget(hex_view);
    detailSplitter->setStretchFactor(0, 2);
    detailSplitter->setStretchFactor(1, 1);
    splitter->addWidget(detailSplitter);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);
    mainLayout->addWidget(splitter, 1);

    status_label = new QLabel(owner);
    status_label->setObjectName(QStringLiteral("rawDataParserStatusLabel"));
    status_label->setWordWrap(true);
    mainLayout->addWidget(status_label);

    scan_watcher = new QFutureWatcher<RawScanResult>(owner);
    QObject::connect(scan_watcher, &QFutureWatcher<RawScanResult>::finished, owner, [this]() {
        finishScanSession();
    });
    scan_progress_timer = new QTimer(owner);
    scan_progress_timer->setInterval(120);
    QObject::connect(scan_progress_timer, &QTimer::timeout, owner, [this]() {
        updateScanProgress();
    });

    QObject::connect(reload_btn, &QPushButton::clicked, owner, [this]() {
        scanSession();
    });
    const auto refilter = [this]() {
        applyFilters();
    };
    QObject::connect(device_combo, &QComboBox::currentIndexChanged, owner, refilter);
    QObject::connect(type_filter, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(time_from, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(time_to, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(seq_from, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(seq_to, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(payload_min, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(payload_max, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(search_edit, &QLineEdit::editingFinished, owner, refilter);
    QObject::connect(abnormal_only, &QCheckBox::toggled, owner, refilter);
    QObject::connect(record_table->selectionModel(), &QItemSelectionModel::currentRowChanged, owner, [this](const QModelIndex&, const QModelIndex&) {
        showSelectedRecord();
    });
    QObject::connect(detail_tree, &QTreeWidget::currentItemChanged, owner, [this](QTreeWidgetItem *current, QTreeWidgetItem*) {
        if (!current)
        {
            return;
        }
        highlightHexRange(current->data(0, Qt::UserRole).toInt(), current->data(0, Qt::UserRole + 1).toInt());
    });
    QObject::connect(export_csv_btn, &QPushButton::clicked, owner, [this]() { exportFilteredCsv(); });
    QObject::connect(export_json_btn, &QPushButton::clicked, owner, [this]() { exportSelectedJson(); });
    QObject::connect(export_bin_btn, &QPushButton::clicked, owner, [this]() { exportSelectedPayload(); });
    QObject::connect(export_decoded_csv_btn, &QPushButton::clicked, owner, [this]() { exportDecodedCsv(); });
    QObject::connect(export_decoded_json_btn, &QPushButton::clicked, owner, [this]() { exportDecodedJson(); });
}

void RawDataParserWindow::Impl::shutdown()
{
    if (scan_progress_timer)
    {
        scan_progress_timer->stop();
    }
    if (scan_progress)
    {
        scan_progress->cancelled.store(true, std::memory_order_relaxed);
    }
    if (!scan_watcher)
    {
        return;
    }
    QObject::disconnect(scan_watcher, nullptr, owner, nullptr);
    if (scan_watcher->isRunning())
    {
        scan_watcher->future().waitForFinished();
    }
    delete scan_watcher;
    scan_watcher = nullptr;
    scan_progress.reset();
}

void RawDataParserWindow::Impl::setEnglish(bool value)
{
    english = value;
    owner->setWindowTitle(english ? QStringLiteral("Raw Data Parser") : QStringLiteral("原始数据解析器"));
    reload_btn->setText(english ? QStringLiteral("Reload") : QStringLiteral("重新加载"));
    export_csv_btn->setText(english ? QStringLiteral("Export List CSV") : QStringLiteral("导出列表CSV"));
    export_json_btn->setText(english ? QStringLiteral("Export Selected JSON") : QStringLiteral("导出选中JSON"));
    export_bin_btn->setText(english ? QStringLiteral("Export Selected BIN") : QStringLiteral("导出选中BIN"));
    export_decoded_csv_btn->setText(english ? QStringLiteral("Export Decoded CSV") : QStringLiteral("导出解析CSV"));
    export_decoded_json_btn->setText(english ? QStringLiteral("Export Decoded JSON") : QStringLiteral("导出解析JSON"));
    abnormal_only->setText(english ? QStringLiteral("Issues only") : QStringLiteral("只看异常"));
    type_filter->setPlaceholderText(english ? QStringLiteral("type, e.g. 0x40") : QStringLiteral("类型，如 0x40"));
    time_from->setPlaceholderText(english ? QStringLiteral("from us") : QStringLiteral("起始us"));
    time_to->setPlaceholderText(english ? QStringLiteral("to us") : QStringLiteral("结束us"));
    seq_from->setPlaceholderText(english ? QStringLiteral("seq from") : QStringLiteral("序号起"));
    seq_to->setPlaceholderText(english ? QStringLiteral("seq to") : QStringLiteral("序号止"));
    payload_min->setPlaceholderText(english ? QStringLiteral("min bytes") : QStringLiteral("最小字节"));
    payload_max->setPlaceholderText(english ? QStringLiteral("max bytes") : QStringLiteral("最大字节"));
    search_edit->setPlaceholderText(english ? QStringLiteral("search metadata or decoded fields") : QStringLiteral("搜索元数据或解析字段"));
    if (scan_watcher && scan_watcher->isRunning())
    {
        updateScanProgress();
    }
    else if (records.isEmpty())
    {
        status_label->setText(english ? QStringLiteral("Open a session to inspect raw DAT records.") : QStringLiteral("打开会话后可查看 raw DAT 原始记录。"));
    }
    refreshDeviceFilter();
    refreshFileList();
}

QString RawDataParserWindow::Impl::resolveSessionDirectory(const QString& path) const
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

bool RawDataParserWindow::Impl::openSessionPath(const QString& path)
{
    const QString resolved = resolveSessionDirectory(path);
    if (resolved.isEmpty())
    {
        status_label->setText(english ? QStringLiteral("Invalid session path.") : QStringLiteral("无效的会话路径。"));
        return false;
    }
    session_directory = resolved;
    scanSession();
    return true;
}

void RawDataParserWindow::Impl::scanSession()
{
    if (scan_watcher && scan_watcher->isRunning())
    {
        updateScanProgress();
        return;
    }

    records.clear();
    visible_rows.clear();
    file_summaries.clear();
    decode_cache.clear();
    current_payload.clear();
    current_hex_positions.clear();
    detail_tree->clear();
    hex_view->clear();
    file_list->clear();
    refreshDeviceFilter();
    record_model->setSource(&records, &visible_rows);
    if (scan_progress_panel)
    {
        scan_progress_panel->setVisible(false);
    }

    if (session_directory.isEmpty())
    {
        status_label->setText(english ? QStringLiteral("No session path is available.") : QStringLiteral("当前没有可用会话路径。"));
        return;
    }

    setScanControlsEnabled(false);
    scan_progress = std::make_shared<RawScanProgress>();
    scan_progress->cancelled.store(false, std::memory_order_relaxed);
    if (scan_progress_panel)
    {
        scan_progress_panel->setVisible(true);
    }
    if (scan_progress_bar)
    {
        scan_progress_bar->setRange(0, 0);
        scan_progress_bar->setFormat(QString());
    }
    status_label->setText(english
        ? QStringLiteral("Indexing raw DAT records...")
        : QStringLiteral("正在建立 raw DAT 原始记录索引..."));
    updateScanProgress();
    if (scan_progress_timer)
    {
        scan_progress_timer->start();
    }

    const QString scanDirectory = session_directory;
    const bool scanEnglish = english;
    const std::shared_ptr<RawScanProgress> progress = scan_progress;
    scan_watcher->setFuture(QtConcurrent::run([scanDirectory, scanEnglish, progress]() {
        return scanRawSession(scanDirectory, scanEnglish, progress);
    }));
}

void RawDataParserWindow::Impl::scanRawFile(const QString& filename, quint16 expectedSourceId)
{
    RawScanResult partial;
    scanRawFileIndex(filename, expectedSourceId, english, partial, nullptr);
    records += partial.records;
    file_summaries += partial.file_summaries;
}

void RawDataParserWindow::Impl::finishScanSession()
{
    if (scan_progress_timer)
    {
        scan_progress_timer->stop();
    }
    updateScanProgress();
    setScanControlsEnabled(true);
    if (!scan_watcher)
    {
        return;
    }

    RawScanResult result = scan_watcher->result();
    if (result.session_directory != session_directory)
    {
        scanSession();
        return;
    }

    records = std::move(result.records);
    file_summaries = std::move(result.file_summaries);
    visible_rows.clear();
    decode_cache.clear();
    current_payload.clear();
    current_hex_positions.clear();

    refreshFileList();
    refreshDeviceFilter();
    applyFilters();
    if (scan_progress_panel)
    {
        scan_progress_panel->setVisible(false);
    }
    scan_progress.reset();
    const QString indexedTemplate = english
        ? QStringLiteral("Indexed %1 raw records from %2.")
        : QStringLiteral("已从 %2 建立 %1 条原始记录索引。");
    status_label->setText(indexedTemplate
        .arg(records.size())
        .arg(QDir::toNativeSeparators(session_directory)));
}

void RawDataParserWindow::Impl::setScanControlsEnabled(bool enabled)
{
    for (QWidget *widget : {
             static_cast<QWidget *>(device_combo),
             static_cast<QWidget *>(type_filter),
             static_cast<QWidget *>(time_from),
             static_cast<QWidget *>(time_to),
             static_cast<QWidget *>(seq_from),
             static_cast<QWidget *>(seq_to),
             static_cast<QWidget *>(payload_min),
             static_cast<QWidget *>(payload_max),
             static_cast<QWidget *>(search_edit),
             static_cast<QWidget *>(abnormal_only),
             static_cast<QWidget *>(reload_btn),
             static_cast<QWidget *>(export_csv_btn),
             static_cast<QWidget *>(export_json_btn),
             static_cast<QWidget *>(export_bin_btn),
             static_cast<QWidget *>(export_decoded_csv_btn),
             static_cast<QWidget *>(export_decoded_json_btn),
         })
    {
        if (widget)
        {
            widget->setEnabled(enabled);
        }
    }
}

void RawDataParserWindow::Impl::updateScanProgress()
{
    if (!scan_progress || !scan_progress_label || !scan_progress_bar)
    {
        return;
    }

    const qint64 totalBytes = scan_progress->total_bytes.load(std::memory_order_relaxed);
    const qint64 scannedBytes = scan_progress->scanned_bytes.load(std::memory_order_relaxed);
    const qint64 recordsIndexed = scan_progress->indexed_records.load(std::memory_order_relaxed);
    const int filesDone = scan_progress->completed_files.load(std::memory_order_relaxed);
    const int filesTotal = scan_progress->total_files.load(std::memory_order_relaxed);

    if (totalBytes > 0)
    {
        const qint64 clampedBytes = std::min(scannedBytes, totalBytes);
        const int progressValue = static_cast<int>((clampedBytes * 1000) / totalBytes);
        scan_progress_bar->setRange(0, 1000);
        scan_progress_bar->setValue(std::min(progressValue, 1000));
        scan_progress_bar->setFormat(QStringLiteral("%p%"));
        scan_progress_label->setText((english
            ? QStringLiteral("Indexing raw DAT records: %1 / %2, %3 records, %4 / %5 files")
            : QStringLiteral("正在建立 raw DAT 原始记录索引：%1 / %2，%3 条记录，%4 / %5 个文件"))
            .arg(formatByteCount(clampedBytes),
                 formatByteCount(totalBytes),
                 QString::number(recordsIndexed),
                 QString::number(filesDone),
                 QString::number(filesTotal)));
    }
    else
    {
        scan_progress_bar->setRange(0, 0);
        scan_progress_bar->setFormat(QString());
        scan_progress_label->setText(english
            ? QStringLiteral("Looking for raw DAT files...")
            : QStringLiteral("正在查找 raw DAT 原始数据文件..."));
    }

    status_label->setText(english
        ? QStringLiteral("Raw data parser is loading. You can keep using the main window.")
        : QStringLiteral("原始数据解析器正在加载，可继续使用主窗口。"));
}

void RawDataParserWindow::Impl::refreshFileList()
{
    file_list->clear();
    for (const RawFileSummary& summary : file_summaries)
    {
        const QString text = QStringLiteral("%1\n%2\n%3 records, %4 bytes\n%5 - %6\n%7")
            .arg(summary.device_name)
            .arg(QDir::toNativeSeparators(summary.filename))
            .arg(summary.record_count)
            .arg(summary.file_size)
            .arg(formatTimestamp(summary.first_timestamp_us))
            .arg(formatTimestamp(summary.last_timestamp_us))
            .arg(summary.status);
        file_list->addItem(text);
    }
}

void RawDataParserWindow::Impl::refreshDeviceFilter()
{
    const int previous = device_combo->currentData().toInt();
    const QSignalBlocker blocker(device_combo);
    device_combo->clear();
    device_combo->addItem(english ? QStringLiteral("All devices") : QStringLiteral("全部设备"), 0);
    for (quint16 sourceId : {kRawSourceEpsilon, kRawSourcePtb, kRawSourceHmp, kRawSourceLidar, kRawSourceTcpWave})
    {
        device_combo->addItem(sourceName(sourceId, english), sourceId);
    }
    const int index = device_combo->findData(previous);
    if (index >= 0)
    {
        device_combo->setCurrentIndex(index);
    }
}

bool RawDataParserWindow::Impl::parseFilterNumber(const QLineEdit *edit, quint64& value) const
{
    value = 0;
    const QString text = edit->text().trimmed();
    if (text.isEmpty())
    {
        return false;
    }
    bool ok = false;
    value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        ? text.mid(2).toULongLong(&ok, 16)
        : text.toULongLong(&ok, 10);
    return ok;
}

bool RawDataParserWindow::Impl::parseTypeFilter(quint16& value) const
{
    quint64 parsed = 0;
    if (!parseFilterNumber(type_filter, parsed))
    {
        return false;
    }
    value = static_cast<quint16>(parsed & 0xFFFFu);
    return true;
}

int RawDataParserWindow::Impl::selectedSourceId() const
{
    return device_combo->currentData().toInt();
}

QString RawDataParserWindow::Impl::cacheKey(const RawRecordIndex& record) const
{
    return QStringLiteral("%1:%2").arg(record.filename).arg(record.payload_offset);
}

QByteArray RawDataParserWindow::Impl::readPayload(const RawRecordIndex& record) const
{
    QFile file(record.filename);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(static_cast<qint64>(record.payload_offset)))
    {
        return QByteArray();
    }
    return file.read(static_cast<qint64>(record.payload_size));
}

RawDecodedRecord RawDataParserWindow::Impl::decodeRecord(const RawRecordIndex& record)
{
    const QString key = cacheKey(record);
    const auto cached = decode_cache.constFind(key);
    if (cached != decode_cache.constEnd())
    {
        return cached.value();
    }
    const QByteArray payload = readPayload(record);
    RawDecodedRecord decoded = decodeRawRecord(record, payload, english);
    decode_cache.insert(key, decoded);
    return decoded;
}

bool RawDataParserWindow::Impl::recordMatches(const RawRecordIndex& record, const QString& searchLower, bool requireDecoded) const
{
    const int selectedSource = selectedSourceId();
    if (selectedSource > 0 && record.source_id != static_cast<quint16>(selectedSource))
    {
        return false;
    }

    quint16 typeValue = 0;
    if (parseTypeFilter(typeValue) && record.record_type != typeValue)
    {
        return false;
    }

    quint64 value = 0;
    if (parseFilterNumber(time_from, value) && record.host_timestamp_us < value)
    {
        return false;
    }
    if (parseFilterNumber(time_to, value) && record.host_timestamp_us > value)
    {
        return false;
    }
    if (parseFilterNumber(seq_from, value) && record.sequence < value)
    {
        return false;
    }
    if (parseFilterNumber(seq_to, value) && record.sequence > value)
    {
        return false;
    }
    if (parseFilterNumber(payload_min, value) && record.payload_size < value)
    {
        return false;
    }
    if (parseFilterNumber(payload_max, value) && record.payload_size > value)
    {
        return false;
    }

    if (!searchLower.isEmpty())
    {
        const QString metadata = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(record.device_name)
            .arg(recordTypeName(record.source_id, record.record_type, english))
            .arg(formatHex(record.record_type))
            .arg(formatHex(record.flags, 8))
            .arg(QFileInfo(record.filename).fileName())
            .arg(record.sequence)
            .toLower();
        if (!metadata.contains(searchLower))
        {
            RawDataParserWindow::Impl *self = const_cast<RawDataParserWindow::Impl*>(this);
            const RawDecodedRecord decoded = self->decodeRecord(record);
            QString decodedText = decoded.title + QLatin1Char(' ') + decoded.status + QLatin1Char(' ') + decoded.summary;
            for (const RawDecodedField& field : decoded.fields)
            {
                decodedText += QLatin1Char(' ') + field.group + QLatin1Char(' ') + field.name + QLatin1Char(' ') + field.raw_value + QLatin1Char(' ') + field.value + QLatin1Char(' ') + field.note;
            }
            if (!decodedText.toLower().contains(searchLower))
            {
                return false;
            }
        }
    }

    if (requireDecoded)
    {
        RawDataParserWindow::Impl *self = const_cast<RawDataParserWindow::Impl*>(this);
        if (self->decodeRecord(record).ok)
        {
            return false;
        }
    }

    return true;
}

void RawDataParserWindow::Impl::applyFilters()
{
    visible_rows.clear();
    const QString searchLower = search_edit->text().trimmed().toLower();
    const bool requireDecoded = abnormal_only->isChecked();
    const bool expensive = requireDecoded || !searchLower.isEmpty();
    const bool largeIndex = records.size() > 50000;
    QProgressDialog *progress = nullptr;
    if ((expensive && records.size() > 1000) || largeIndex)
    {
        const QString progressText = expensive
            ? (english ? QStringLiteral("Scanning decoded records...") : QStringLiteral("正在扫描解析字段..."))
            : (english ? QStringLiteral("Preparing raw record list...") : QStringLiteral("正在准备原始记录列表..."));
        progress = new QProgressDialog(progressText,
                                       english ? QStringLiteral("Cancel") : QStringLiteral("取消"),
                                       0,
                                       records.size(),
                                       owner);
        progress->setWindowTitle(english ? QStringLiteral("Scanning Records") : QStringLiteral("正在扫描记录"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumWidth(380);
        VaporView::installCustomTitleBar(progress, false);
        applyRawDataProgressDialogStyle(progress);
    }

    for (int i = 0; i < records.size(); ++i)
    {
        if (progress && (i % 250 == 0))
        {
            progress->setValue(i);
            QApplication::processEvents();
            if (progress->wasCanceled())
            {
                break;
            }
        }
        if (recordMatches(records.at(i), searchLower, requireDecoded))
        {
            visible_rows.push_back(i);
        }
    }
    if (progress)
    {
        progress->setValue(records.size());
        progress->close();
        delete progress;
    }
    record_model->setSource(&records, &visible_rows);
    if (!visible_rows.isEmpty())
    {
        const QModelIndex first = record_model->index(0, 0);
        record_table->setCurrentIndex(first);
        record_table->selectRow(0);
        showSelectedRecord();
    }
    else
    {
        detail_tree->clear();
        hex_view->clear();
    }
    status_label->setText((english
        ? QStringLiteral("Showing %1 / %2 indexed records.")
        : QStringLiteral("正在显示 %1 / %2 条索引记录。"))
        .arg(visible_rows.size())
        .arg(records.size()));
}

void RawDataParserWindow::Impl::showSelectedRecord()
{
    const QModelIndex current = record_table->currentIndex();
    if (!current.isValid())
    {
        return;
    }
    const int recordIndex = current.sibling(current.row(), 0).data(Qt::UserRole).toInt();
    if (recordIndex < 0 || recordIndex >= records.size())
    {
        return;
    }
    const RawRecordIndex& record = records.at(recordIndex);
    current_payload = readPayload(record);
    const RawDecodedRecord decoded = decodeRecord(record);
    showDecodedRecord(record, current_payload, decoded);
}

void RawDataParserWindow::Impl::showDecodedRecord(const RawRecordIndex& record, const QByteArray& payload, const RawDecodedRecord& decoded)
{
    detail_tree->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    for (const RawDecodedField& field : decoded.fields)
    {
        QTreeWidgetItem *groupItem = groups.value(field.group, nullptr);
        if (!groupItem)
        {
            groupItem = new QTreeWidgetItem(detail_tree, QStringList{field.group});
            groupItem->setFirstColumnSpanned(true);
            groups.insert(field.group, groupItem);
        }
        auto *item = new QTreeWidgetItem(groupItem);
        item->setText(0, field.name);
        item->setText(1, field.raw_value);
        item->setText(2, field.value);
        item->setText(3, field.unit);
        item->setText(4, field.offset >= 0 ? QStringLiteral("%1 +%2").arg(field.offset).arg(field.length) : QString());
        item->setText(5, field.note);
        item->setData(0, Qt::UserRole, field.offset);
        item->setData(0, Qt::UserRole + 1, field.length);
        if (field.abnormal)
        {
            for (int col = 0; col < detail_tree->columnCount(); ++col)
            {
                item->setBackground(col, appThemeColor(AppThemeColor::ErrorHighlight, false));
            }
        }
    }
    detail_tree->expandAll();
    setHexPayload(payload);
    status_label->setText(QStringLiteral("%1 | %2 | %3")
        .arg(record.device_name, decoded.status, decoded.summary));
}

void RawDataParserWindow::Impl::setHexPayload(const QByteArray& payload)
{
    current_hex_positions.clear();
    const int shownBytes = std::min<int>(static_cast<int>(payload.size()), kMaxHexBytes);
    current_hex_positions.resize(shownBytes);
    QString text;
    text.reserve(shownBytes * 4);
    for (int offset = 0; offset < shownBytes; offset += 16)
    {
        text += QStringLiteral("%1  ").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
        const int lineEnd = std::min(offset + 16, shownBytes);
        for (int i = offset; i < lineEnd; ++i)
        {
            const int start = text.size();
            text += QStringLiteral("%1").arg(static_cast<unsigned char>(payload.at(i)), 2, 16, QLatin1Char('0')).toUpper();
            current_hex_positions[i] = {start, 2};
            text += QLatin1Char(' ');
        }
        text += QLatin1Char('\n');
    }
    if (payload.size() > shownBytes)
    {
        text += (english
                    ? QStringLiteral("\n... hex preview truncated to %1 of %2 bytes.\n")
                    : QStringLiteral("\n... Hex 预览已截断，仅显示 %1 / %2 字节。\n"))
                    .arg(shownBytes)
                    .arg(payload.size());
    }
    hex_view->setPlainText(text);
}

void RawDataParserWindow::Impl::highlightHexRange(int offset, int length)
{
    QList<QTextEdit::ExtraSelection> selections;
    if (offset >= 0 && length > 0)
    {
        const int end = std::min<int>(offset + length, static_cast<int>(current_hex_positions.size()));
        QTextCharFormat format;
        format.setBackground(appThemeColor(AppThemeColor::SearchHighlight, false));
        for (int i = offset; i < end; ++i)
        {
            QTextEdit::ExtraSelection selection;
            QTextCursor cursor(hex_view->document());
            cursor.setPosition(current_hex_positions.at(i).first);
            cursor.setPosition(current_hex_positions.at(i).first + current_hex_positions.at(i).second, QTextCursor::KeepAnchor);
            selection.cursor = cursor;
            selection.format = format;
            selections.push_back(selection);
        }
    }
    hex_view->setExtraSelections(selections);
}

int RawDataParserWindow::Impl::currentRecordIndex() const
{
    if (record_table && record_table->selectionModel())
    {
        const QModelIndexList selectedRows = record_table->selectionModel()->selectedRows();
        if (!selectedRows.isEmpty())
        {
            return selectedRows.first().data(Qt::UserRole).toInt();
        }
    }

    const QModelIndex current = record_table ? record_table->currentIndex() : QModelIndex();
    if (!current.isValid())
    {
        return -1;
    }
    return current.sibling(current.row(), 0).data(Qt::UserRole).toInt();
}

void RawDataParserWindow::Impl::exportFilteredCsv()
{
    const QString filename = QFileDialog::getSaveFileName(owner,
        english ? QStringLiteral("Export Raw Record List CSV") : QStringLiteral("导出原始记录列表CSV"),
        QDir(session_directory).filePath(QStringLiteral("raw_records.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (filename.isEmpty())
    {
        return;
    }

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to open export file.") : QStringLiteral("无法打开导出文件。"));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "row,timestamp_us,time,device,source_id,record_type,flags,sequence,payload_size,file,record_offset,payload_offset\n";
    for (int row = 0; row < visible_rows.size(); ++row)
    {
        const RawRecordIndex& record = records.at(visible_rows.at(row));
        out << row + 1 << ','
            << record.host_timestamp_us << ','
            << csvEscape(formatTimestamp(record.host_timestamp_us)) << ','
            << csvEscape(record.device_name) << ','
            << record.source_id << ','
            << csvEscape(formatHex(record.record_type)) << ','
            << csvEscape(formatHex(record.flags, 8)) << ','
            << record.sequence << ','
            << record.payload_size << ','
            << csvEscape(QDir::toNativeSeparators(record.filename)) << ','
            << record.record_offset << ','
            << record.payload_offset << '\n';
    }
    if (!file.commit())
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to save CSV file.") : QStringLiteral("保存 CSV 文件失败。"));
    }
}

QJsonObject RawDataParserWindow::Impl::decodedRecordToJson(const RawRecordIndex& record, const RawDecodedRecord& decoded) const
{
    QJsonObject root;
    root["title"] = decoded.title;
    root["ok"] = decoded.ok;
    root["status"] = decoded.status;
    root["summary"] = decoded.summary;
    root["file"] = QDir::toNativeSeparators(record.filename);
    root["timestamp_us"] = QString::number(record.host_timestamp_us);
    root["source_id"] = static_cast<int>(record.source_id);
    root["record_type"] = static_cast<int>(record.record_type);
    root["flags"] = static_cast<int>(record.flags);
    root["sequence"] = QString::number(record.sequence);
    root["payload_size"] = static_cast<int>(record.payload_size);
    QJsonArray fields;
    for (const RawDecodedField& field : decoded.fields)
    {
        QJsonObject object;
        object["group"] = field.group;
        object["name"] = field.name;
        object["raw"] = field.raw_value;
        object["value"] = field.value;
        object["unit"] = field.unit;
        object["offset"] = field.offset;
        object["length"] = field.length;
        object["note"] = field.note;
        object["abnormal"] = field.abnormal;
        fields.push_back(object);
    }
    root["fields"] = fields;
    return root;
}

void RawDataParserWindow::Impl::exportSelectedJson()
{
    const int recordIndex = currentRecordIndex();
    if (recordIndex < 0 || recordIndex >= records.size())
    {
        QMessageBox::information(owner,
            owner->windowTitle(),
            english ? QStringLiteral("Select one raw record before exporting JSON.")
                    : QStringLiteral("请先在记录列表中选择一条原始记录，再导出 JSON。"));
        return;
    }
    const RawRecordIndex& record = records.at(recordIndex);
    const RawDecodedRecord decoded = decodeRecord(record);

    const QString filename = QFileDialog::getSaveFileName(owner,
        english ? QStringLiteral("Export Selected Decoded Record JSON") : QStringLiteral("导出选中解析记录JSON"),
        QDir(session_directory).filePath(QStringLiteral("raw_record_%1.json").arg(record.sequence)),
        QStringLiteral("JSON (*.json)"));
    if (filename.isEmpty())
    {
        return;
    }

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to open export file.") : QStringLiteral("无法打开导出文件。"));
        return;
    }
    file.write(QJsonDocument(decodedRecordToJson(record, decoded)).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to save JSON file.") : QStringLiteral("保存 JSON 文件失败。"));
    }
}

void RawDataParserWindow::Impl::exportSelectedPayload()
{
    const int recordIndex = currentRecordIndex();
    if (recordIndex < 0 || recordIndex >= records.size())
    {
        QMessageBox::information(owner,
            owner->windowTitle(),
            english ? QStringLiteral("Select one raw record before exporting BIN.")
                    : QStringLiteral("请先在记录列表中选择一条原始记录，再导出 BIN。"));
        return;
    }
    const RawRecordIndex& record = records.at(recordIndex);
    const QByteArray payload = readPayload(record);
    const QString filename = QFileDialog::getSaveFileName(owner,
        english ? QStringLiteral("Export Raw Payload") : QStringLiteral("导出原始Payload"),
        QDir(session_directory).filePath(QStringLiteral("raw_payload_%1.bin").arg(record.sequence)),
        QStringLiteral("Binary (*.bin)"));
    if (filename.isEmpty())
    {
        return;
    }
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to open export file.") : QStringLiteral("无法打开导出文件。"));
        return;
    }
    file.write(payload);
    if (!file.commit())
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to save BIN file.") : QStringLiteral("保存 BIN 文件失败。"));
    }
}

void RawDataParserWindow::Impl::exportDecodedCsv()
{
    if (visible_rows.isEmpty())
    {
        QMessageBox::information(owner,
            owner->windowTitle(),
            english ? QStringLiteral("No filtered records to export.")
                    : QStringLiteral("当前过滤结果为空，没有可导出的解析记录。"));
        return;
    }

    const QString filename = QFileDialog::getSaveFileName(owner,
        english ? QStringLiteral("Export Decoded Fields CSV") : QStringLiteral("导出解析字段CSV"),
        QDir(session_directory).filePath(QStringLiteral("raw_decoded_fields.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (filename.isEmpty())
    {
        return;
    }

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to open export file.") : QStringLiteral("无法打开导出文件。"));
        return;
    }

    QProgressDialog progress(english ? QStringLiteral("Exporting decoded fields...") : QStringLiteral("正在导出解析字段..."),
                             english ? QStringLiteral("Cancel") : QStringLiteral("取消"),
                             0,
                             visible_rows.size(),
                             owner);
    progress.setWindowTitle(english ? QStringLiteral("Export Decoded Fields") : QStringLiteral("导出解析字段"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumWidth(380);
    VaporView::installCustomTitleBar(&progress, false);
    applyRawDataProgressDialogStyle(&progress);

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "row,timestamp_us,time,device,source_id,record_type,flags,sequence,payload_size,file,record_offset,payload_offset,record_title,record_ok,record_status,record_summary,field_group,field_name,field_raw,field_value,field_unit,field_offset,field_length,field_note,field_abnormal\n";
    for (int row = 0; row < visible_rows.size(); ++row)
    {
        if (row % 100 == 0)
        {
            progress.setValue(row);
            QApplication::processEvents();
            if (progress.wasCanceled())
            {
                return;
            }
        }

        const RawRecordIndex& record = records.at(visible_rows.at(row));
        const RawDecodedRecord decoded = decodeRecord(record);
        for (const RawDecodedField& field : decoded.fields)
        {
            out << row + 1 << ','
                << record.host_timestamp_us << ','
                << csvEscape(formatTimestamp(record.host_timestamp_us)) << ','
                << csvEscape(record.device_name) << ','
                << record.source_id << ','
                << csvEscape(formatHex(record.record_type)) << ','
                << csvEscape(formatHex(record.flags, 8)) << ','
                << record.sequence << ','
                << record.payload_size << ','
                << csvEscape(QDir::toNativeSeparators(record.filename)) << ','
                << record.record_offset << ','
                << record.payload_offset << ','
                << csvEscape(decoded.title) << ','
                << (decoded.ok ? 1 : 0) << ','
                << csvEscape(decoded.status) << ','
                << csvEscape(decoded.summary) << ','
                << csvEscape(field.group) << ','
                << csvEscape(field.name) << ','
                << csvEscape(field.raw_value) << ','
                << csvEscape(field.value) << ','
                << csvEscape(field.unit) << ','
                << field.offset << ','
                << field.length << ','
                << csvEscape(field.note) << ','
                << (field.abnormal ? 1 : 0) << '\n';
        }
    }
    progress.setValue(visible_rows.size());
    if (!file.commit())
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to save decoded CSV file.") : QStringLiteral("保存解析 CSV 文件失败。"));
    }
}

void RawDataParserWindow::Impl::exportDecodedJson()
{
    if (visible_rows.isEmpty())
    {
        QMessageBox::information(owner,
            owner->windowTitle(),
            english ? QStringLiteral("No filtered records to export.")
                    : QStringLiteral("当前过滤结果为空，没有可导出的解析记录。"));
        return;
    }

    const QString filename = QFileDialog::getSaveFileName(owner,
        english ? QStringLiteral("Export Decoded Records JSON") : QStringLiteral("导出解析记录JSON"),
        QDir(session_directory).filePath(QStringLiteral("raw_decoded_records.json")),
        QStringLiteral("JSON (*.json)"));
    if (filename.isEmpty())
    {
        return;
    }

    QProgressDialog progress(english ? QStringLiteral("Exporting decoded records...") : QStringLiteral("正在导出解析记录..."),
                             english ? QStringLiteral("Cancel") : QStringLiteral("取消"),
                             0,
                             visible_rows.size(),
                             owner);
    progress.setWindowTitle(english ? QStringLiteral("Export Decoded Records") : QStringLiteral("导出解析记录"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumWidth(380);
    VaporView::installCustomTitleBar(&progress, false);
    applyRawDataProgressDialogStyle(&progress);

    QJsonArray recordArray;
    for (int row = 0; row < visible_rows.size(); ++row)
    {
        if (row % 100 == 0)
        {
            progress.setValue(row);
            QApplication::processEvents();
            if (progress.wasCanceled())
            {
                return;
            }
        }
        const RawRecordIndex& record = records.at(visible_rows.at(row));
        recordArray.push_back(decodedRecordToJson(record, decodeRecord(record)));
    }
    progress.setValue(visible_rows.size());

    QJsonObject root;
    root["session"] = QDir::toNativeSeparators(session_directory);
    root["filtered_record_count"] = visible_rows.size();
    root["records"] = recordArray;

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to open export file.") : QStringLiteral("无法打开导出文件。"));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        QMessageBox::warning(owner, owner->windowTitle(), english ? QStringLiteral("Failed to save decoded JSON file.") : QStringLiteral("保存解析 JSON 文件失败。"));
    }
}

namespace
{

void addUnifiedRecordFields(RawDecodedRecord& decoded, const RawRecordIndex& record)
{
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("source_id"), QString::number(record.source_id), record.device_name, QString(), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("record_type"), formatHex(record.record_type), recordTypeName(record.source_id, record.record_type, false), QString(), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("flags"), formatHex(record.flags, 8), QString::number(record.flags), QString(), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("sequence"), QString::number(record.sequence), QString(), QString(), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("host_timestamp_us"), QString::number(record.host_timestamp_us), formatTimestamp(record.host_timestamp_us), QStringLiteral("UTC"), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("payload_size"), QString::number(record.payload_size), QString(), QStringLiteral("bytes"), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("record_offset"), QString::number(record.record_offset), QString(), QStringLiteral("bytes"), -1, 0);
    addField(decoded, QStringLiteral("Unified Raw Record"), QStringLiteral("payload_offset"), QString::number(record.payload_offset), QString(), QStringLiteral("bytes"), -1, 0);
}

void decodeEpsilonPayload(RawDecodedRecord& decoded, const QByteArray& payload, bool english)
{
    decoded.title = QStringLiteral("EPSILON FDILink");
    if (payload.size() < 8)
    {
        addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("frame"), QString::number(payload.size()), QString(), QStringLiteral("bytes"), 0, payload.size(), english ? QStringLiteral("Frame is shorter than minimum 8 bytes.") : QStringLiteral("帧长度小于最小 8 字节。"), true);
        decoded.status = english ? QStringLiteral("Invalid") : QStringLiteral("无效");
        decoded.summary = english ? QStringLiteral("Too short") : QStringLiteral("长度过短");
        return;
    }

    const auto bytes = reinterpret_cast<const uint8_t*>(payload.constData());
    const uint8_t packetId = bytes[1];
    const uint8_t payloadSize = bytes[2];
    const uint8_t serial = bytes[3];
    const uint8_t crc8 = bytes[4];
    const uint16_t crc16 = static_cast<uint16_t>((static_cast<uint16_t>(bytes[5]) << 8) | bytes[6]);
    const int expectedFrameSize = 8 + payloadSize;
    const bool headOk = bytes[0] == kFdilinkFrameHead;
    const bool sizeOk = payload.size() == expectedFrameSize;
    const bool crc8Ok = fdilinkCrc8(bytes, 4) == crc8;
    const bool crc16Ok = sizeOk && fdilinkCrc16(bytes + 7, payloadSize) == crc16;
    const bool tailOk = sizeOk && bytes[payload.size() - 1] == kFdilinkFrameTail;

    addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("head"), formatHex(bytes[0]), headOk ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 0, 1, QStringLiteral("0xFC"), !headOk);
    addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("packet_id"), formatHex(packetId), epsilonPacketName(packetId), QString(), 1, 1);
    addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("payload_size"), QString::number(payloadSize), QString(), QStringLiteral("bytes"), 2, 1, QStringLiteral("Frame size should be payload + 8"), !sizeOk);
    addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("serial_number"), QString::number(serial), formatHex(serial), QString(), 3, 1);
    addField(decoded, QStringLiteral("CRC"), QStringLiteral("crc8"), formatHex(crc8), crc8Ok ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 4, 1, QString(), !crc8Ok);
    addField(decoded, QStringLiteral("CRC"), QStringLiteral("crc16"), formatHex(crc16, 4), crc16Ok ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 5, 2, QString(), !crc16Ok);
    addField(decoded, QStringLiteral("FDILink Header"), QStringLiteral("tail"), sizeOk ? formatHex(static_cast<unsigned char>(payload.at(payload.size() - 1))) : QStringLiteral("---"), tailOk ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), sizeOk ? payload.size() - 1 : -1, sizeOk ? 1 : 0, QStringLiteral("0xFD"), !tailOk);

    const uint8_t* p = bytes + 7;
    const int payloadOffset = 7;
    const auto has = [payloadSize](int count) {
        return payloadSize >= count;
    };

    if (packetId == 0x40 && has(56))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("gyro_x"), readFloatLE(p + 0), QStringLiteral("rad/s"), payloadOffset + 0, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("gyro_y"), readFloatLE(p + 4), QStringLiteral("rad/s"), payloadOffset + 4, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("gyro_z"), readFloatLE(p + 8), QStringLiteral("rad/s"), payloadOffset + 8, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("acc_x"), readFloatLE(p + 12), QStringLiteral("m/s^2"), payloadOffset + 12, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("acc_y"), readFloatLE(p + 16), QStringLiteral("m/s^2"), payloadOffset + 16, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("acc_z"), readFloatLE(p + 20), QStringLiteral("m/s^2"), payloadOffset + 20, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("mag_x"), readFloatLE(p + 24), QStringLiteral("mG"), payloadOffset + 24, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("mag_y"), readFloatLE(p + 28), QStringLiteral("mG"), payloadOffset + 28, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("mag_z"), readFloatLE(p + 32), QStringLiteral("mG"), payloadOffset + 32, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("device_timestamp_us"), QString::number(readI64LE(p + 48)), QString(), QStringLiteral("us"), payloadOffset + 48, 8);
    }
    else if (packetId == 0x41 && has(48))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ang_vel_x"), readFloatLE(p + 0), QStringLiteral("rad/s"), payloadOffset + 0, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ang_vel_y"), readFloatLE(p + 4), QStringLiteral("rad/s"), payloadOffset + 4, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ang_vel_z"), readFloatLE(p + 8), QStringLiteral("rad/s"), payloadOffset + 8, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("roll"), radToDeg(readFloatLE(p + 12)), QStringLiteral("deg"), payloadOffset + 12, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("pitch"), radToDeg(readFloatLE(p + 16)), QStringLiteral("deg"), payloadOffset + 16, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("yaw"), radToDeg(readFloatLE(p + 20)), QStringLiteral("deg"), payloadOffset + 20, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("quat_w"), readFloatLE(p + 24), QString(), payloadOffset + 24, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("quat_x"), readFloatLE(p + 28), QString(), payloadOffset + 28, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("quat_y"), readFloatLE(p + 32), QString(), payloadOffset + 32, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("quat_z"), readFloatLE(p + 36), QString(), payloadOffset + 36, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("device_timestamp_us"), QString::number(readI64LE(p + 40)), QString(), QStringLiteral("us"), payloadOffset + 40, 8);
    }
    else if (packetId == 0x42 && has(72))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_vel_x"), readFloatLE(p + 0), QStringLiteral("m/s"), payloadOffset + 0, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_vel_y"), readFloatLE(p + 4), QStringLiteral("m/s"), payloadOffset + 4, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_vel_z"), readFloatLE(p + 8), QStringLiteral("m/s"), payloadOffset + 8, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_acc_x"), readFloatLE(p + 12), QStringLiteral("m/s^2"), payloadOffset + 12, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_acc_y"), readFloatLE(p + 16), QStringLiteral("m/s^2"), payloadOffset + 16, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("body_acc_z"), readFloatLE(p + 20), QStringLiteral("m/s^2"), payloadOffset + 20, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ned_n"), readFloatLE(p + 24), QStringLiteral("m"), payloadOffset + 24, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ned_e"), readFloatLE(p + 28), QStringLiteral("m"), payloadOffset + 28, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ned_d"), readFloatLE(p + 32), QStringLiteral("m"), payloadOffset + 32, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_n"), readFloatLE(p + 36), QStringLiteral("m/s"), payloadOffset + 36, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_e"), readFloatLE(p + 40), QStringLiteral("m/s"), payloadOffset + 40, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_d"), readFloatLE(p + 44), QStringLiteral("m/s"), payloadOffset + 44, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("device_timestamp_us"), QString::number(readI64LE(p + 64)), QString(), QStringLiteral("us"), payloadOffset + 64, 8);
    }
    else if (packetId == 0x50 && has(14))
    {
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("system_status_bits"), formatHex(readU16LE(p + 0), 4), QString(), QString(), payloadOffset + 0, 2);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("filter_status_bits"), formatHex(readU16LE(p + 2), 4), QString(), QString(), payloadOffset + 2, 2);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("update_status_bits"), formatHex(readU16LE(p + 4), 4), QString(), QString(), payloadOffset + 4, 2);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_unix_s"), QString::number(readU32LE(p + 6)), formatTimestamp(static_cast<quint64>(readU32LE(p + 6)) * 1000000ULL + readU32LE(p + 10)), QString(), payloadOffset + 6, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_microseconds"), QString::number(readU32LE(p + 10)), QString(), QStringLiteral("us"), payloadOffset + 10, 4);
        if (has(38))
        {
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("latitude"), radToDeg(readDoubleLE(p + 14)), QStringLiteral("deg"), payloadOffset + 14, 8, 8);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("longitude"), radToDeg(readDoubleLE(p + 22)), QStringLiteral("deg"), payloadOffset + 22, 8, 8);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("height"), readDoubleLE(p + 30), QStringLiteral("m"), payloadOffset + 30, 8);
        }
        if (has(50))
        {
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_n"), readFloatLE(p + 38), QStringLiteral("m/s"), payloadOffset + 38, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_e"), readFloatLE(p + 42), QStringLiteral("m/s"), payloadOffset + 42, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vel_d"), readFloatLE(p + 46), QStringLiteral("m/s"), payloadOffset + 46, 4);
        }
        if (has(102))
        {
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("roll"), radToDeg(readFloatLE(p + 66)), QStringLiteral("deg"), payloadOffset + 66, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("pitch"), radToDeg(readFloatLE(p + 70)), QStringLiteral("deg"), payloadOffset + 70, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("yaw"), radToDeg(readFloatLE(p + 74)), QStringLiteral("deg"), payloadOffset + 74, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("lat_std"), readFloatLE(p + 90), QStringLiteral("m"), payloadOffset + 90, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("lon_std"), readFloatLE(p + 94), QStringLiteral("m"), payloadOffset + 94, 4);
            addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("height_std"), readFloatLE(p + 98), QStringLiteral("m"), payloadOffset + 98, 4);
        }
    }
    else if (packetId == 0x51 && has(8))
    {
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_unix_s"), QString::number(readU32LE(p + 0)), formatTimestamp(static_cast<quint64>(readU32LE(p + 0)) * 1000000ULL + readU32LE(p + 4)), QString(), payloadOffset + 0, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_microseconds"), QString::number(readU32LE(p + 4)), QString(), QStringLiteral("us"), payloadOffset + 4, 4);
    }
    else if (packetId == 0x52 && has(14))
    {
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("microseconds"), QString::number(readU32LE(p + 0)), QString(), QStringLiteral("us"), payloadOffset + 0, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("year"), QString::number(readU16LE(p + 4)), QString(), QString(), payloadOffset + 4, 2);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("month"), QString::number(p[8]), QString(), QString(), payloadOffset + 8, 1);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("day"), QString::number(p[9]), QString(), QString(), payloadOffset + 9, 1);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("hour"), QString::number(p[11]), QString(), QString(), payloadOffset + 11, 1);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("minute"), QString::number(p[12]), QString(), QString(), payloadOffset + 12, 1);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("second"), QString::number(p[13]), QString(), QString(), payloadOffset + 13, 1);
    }
    else if (packetId == 0x59 && has(74))
    {
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_unix_s"), QString::number(readU32LE(p + 0)), QString(), QStringLiteral("s"), payloadOffset + 0, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("utc_microseconds"), QString::number(readU32LE(p + 4)), QString(), QStringLiteral("us"), payloadOffset + 4, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("lat_std"), readFloatLE(p + 44), QStringLiteral("m"), payloadOffset + 44, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("lon_std"), readFloatLE(p + 48), QStringLiteral("m"), payloadOffset + 48, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("height_std"), readFloatLE(p + 52), QStringLiteral("m"), payloadOffset + 52, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("diff_age"), readFloatLE(p + 64), QStringLiteral("s"), payloadOffset + 64, 4);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("raw_gnss_status"), formatHex(readU16LE(p + 72), 4), QString(), QString(), payloadOffset + 72, 2);
    }
    else if (packetId == 0x5A && has(9))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("hdop"), readFloatLE(p + 0), QString(), payloadOffset + 0, 4, 3);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vdop"), readFloatLE(p + 4), QString(), payloadOffset + 4, 4, 3);
        addField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("satellites"), QString::number(p[8]), QString(), QString(), payloadOffset + 8, 1);
    }
    else if (packetId == 0x5C && has(32))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("latitude"), radToDeg(readDoubleLE(p + 0)), QStringLiteral("deg"), payloadOffset + 0, 8, 8);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("longitude"), radToDeg(readDoubleLE(p + 8)), QStringLiteral("deg"), payloadOffset + 8, 8, 8);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("height"), readDoubleLE(p + 16), QStringLiteral("m"), payloadOffset + 16, 8);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("hacc"), readFloatLE(p + 24), QStringLiteral("m"), payloadOffset + 24, 4);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("vacc"), readFloatLE(p + 28), QStringLiteral("m"), payloadOffset + 28, 4);
    }
    else if (packetId == 0x5D && has(24))
    {
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ecef_x"), readDoubleLE(p + 0), QStringLiteral("m"), payloadOffset + 0, 8);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ecef_y"), readDoubleLE(p + 8), QStringLiteral("m"), payloadOffset + 8, 8);
        addNumericField(decoded, QStringLiteral("Payload Fields"), QStringLiteral("ecef_z"), readDoubleLE(p + 16), QStringLiteral("m"), payloadOffset + 16, 8);
    }
    else if (packetId == 0xF0)
    {
        QByteArray mavlink;
        mavlink.resize(payloadSize);
        for (int i = 0; i < payloadSize; ++i)
        {
            mavlink[i] = static_cast<char>(~p[i]);
        }
        int subframeCount = 0;
        for (int offset = 0; offset + 8 <= mavlink.size();)
        {
            if (static_cast<uint8_t>(mavlink.at(offset)) != 0xFEu)
            {
                ++offset;
                continue;
            }
            const int mavPayloadSize = static_cast<uint8_t>(mavlink.at(offset + 1));
            const int frameSize = mavPayloadSize + 8;
            if (offset + frameSize > mavlink.size())
            {
                break;
            }
            const int msgId = static_cast<uint8_t>(mavlink.at(offset + 5));
            addField(decoded, QStringLiteral("MAVLink Tunnel"), QStringLiteral("message"), QString::number(msgId), QStringLiteral("msgid %1, len %2").arg(msgId).arg(mavPayloadSize), QString(), payloadOffset + offset, frameSize);
            ++subframeCount;
            offset += frameSize;
        }
        addField(decoded, QStringLiteral("MAVLink Tunnel"), QStringLiteral("subframe_count"), QString::number(subframeCount), QString(), QString(), payloadOffset, payloadSize);
    }
    else
    {
        addField(decoded, QStringLiteral("Raw Hex"), QStringLiteral("payload_preview"), hexPreview(payload.mid(7, payloadSize)), QString(), QString(), payloadOffset, payloadSize, english ? QStringLiteral("No field map is available for this packet yet.") : QStringLiteral("该 packet 暂无字段映射。"));
    }

    decoded.status = decoded.ok ? (english ? QStringLiteral("OK") : QStringLiteral("正常")) : (english ? QStringLiteral("Issue") : QStringLiteral("异常"));
    decoded.summary = QStringLiteral("%1, serial=%2, payload=%3 bytes").arg(epsilonPacketName(packetId)).arg(serial).arg(payloadSize);
}

void decodePtbPayload(RawDecodedRecord& decoded, const QByteArray& payload, bool english)
{
    decoded.title = QStringLiteral("PTB210");
    const QString rawText = QString::fromLatin1(payload);
    const QString trimmed = rawText.trimmed();
    bool ok = false;
    const double pressure = trimmed.toDouble(&ok);
    addField(decoded, QStringLiteral("PTB210 Response"), QStringLiteral("raw_line"), rawText, trimmed, QString(), 0, payload.size());
    addNumericField(decoded, QStringLiteral("PTB210 Response"), QStringLiteral("pressure"), pressure, QStringLiteral("hPa"), 0, payload.size(), 3);
    if (!ok)
    {
        addField(decoded, QStringLiteral("PTB210 Response"), QStringLiteral("numeric_parse"), trimmed, QStringLiteral("FAILED"), QString(), 0, payload.size(), QString(), true);
    }
    decoded.status = ok ? (english ? QStringLiteral("OK") : QStringLiteral("正常")) : (english ? QStringLiteral("Parse issue") : QStringLiteral("解析异常"));
    decoded.summary = ok ? QStringLiteral("%1 hPa").arg(number(pressure, 3)) : trimmed;
}

void decodeHmpPayload(RawDecodedRecord& decoded, const QByteArray& payload, bool english)
{
    decoded.title = QStringLiteral("HMP3 Modbus RTU");
    if (payload.size() < 5)
    {
        addField(decoded, QStringLiteral("Modbus"), QStringLiteral("frame"), QString::number(payload.size()), QString(), QStringLiteral("bytes"), 0, payload.size(), QStringLiteral("Too short"), true);
        decoded.status = english ? QStringLiteral("Invalid") : QStringLiteral("无效");
        decoded.summary = english ? QStringLiteral("Too short") : QStringLiteral("长度过短");
        return;
    }
    const auto bytes = reinterpret_cast<const uint8_t*>(payload.constData());
    const uint8_t slave = bytes[0];
    const uint8_t functionCode = bytes[1];
    const uint16_t receivedCrc = readU16LE(bytes + payload.size() - 2);
    const uint16_t calculatedCrc = modbusCrc16(bytes, static_cast<size_t>(payload.size() - 2));
    const bool crcOk = receivedCrc == calculatedCrc;
    addField(decoded, QStringLiteral("Modbus"), QStringLiteral("slave"), QString::number(slave), slave == kHmpSlaveAddress ? QStringLiteral("OK") : QStringLiteral("Unexpected"), QString(), 0, 1, QStringLiteral("expected 240"), slave != kHmpSlaveAddress);
    addField(decoded, QStringLiteral("Modbus"), QStringLiteral("function"), formatHex(functionCode), QString(), QString(), 1, 1);
    addField(decoded, QStringLiteral("Modbus"), QStringLiteral("crc"), formatHex(receivedCrc, 4), crcOk ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), payload.size() - 2, 2, QString(), !crcOk);
    if ((functionCode & 0x80u) != 0 && payload.size() >= 5)
    {
        addField(decoded, QStringLiteral("Modbus Exception"), QStringLiteral("exception_code"), formatHex(static_cast<uint8_t>(bytes[2])), QString::number(bytes[2]), QString(), 2, 1, QString(), true);
    }
    else if (payload.size() >= 13)
    {
        const uint8_t byteCount = bytes[2];
        addField(decoded, QStringLiteral("Modbus"), QStringLiteral("byte_count"), QString::number(byteCount), QString(), QStringLiteral("bytes"), 2, 1);
        if (byteCount >= 8)
        {
            const uint16_t reg0 = static_cast<uint16_t>((bytes[3] << 8) | bytes[4]);
            const uint16_t reg1 = static_cast<uint16_t>((bytes[5] << 8) | bytes[6]);
            const uint16_t reg2 = static_cast<uint16_t>((bytes[7] << 8) | bytes[8]);
            const uint16_t reg3 = static_cast<uint16_t>((bytes[9] << 8) | bytes[10]);
            addField(decoded, QStringLiteral("Registers"), QStringLiteral("humidity_reg0"), formatHex(reg0, 4), QString(), QString(), 3, 2);
            addField(decoded, QStringLiteral("Registers"), QStringLiteral("humidity_reg1"), formatHex(reg1, 4), QString(), QString(), 5, 2);
            addField(decoded, QStringLiteral("Registers"), QStringLiteral("temperature_reg0"), formatHex(reg2, 4), QString(), QString(), 7, 2);
            addField(decoded, QStringLiteral("Registers"), QStringLiteral("temperature_reg1"), formatHex(reg3, 4), QString(), QString(), 9, 2);
            addNumericField(decoded, QStringLiteral("Decoded Values"), QStringLiteral("humidity"), decodeModbusFloatLE(reg0, reg1), QStringLiteral("%RH"), 3, 4, 3);
            addNumericField(decoded, QStringLiteral("Decoded Values"), QStringLiteral("temperature"), decodeModbusFloatLE(reg2, reg3), QStringLiteral("degC"), 7, 4, 3);
        }
    }
    decoded.status = decoded.ok ? (english ? QStringLiteral("OK") : QStringLiteral("正常")) : (english ? QStringLiteral("Issue") : QStringLiteral("异常"));
    decoded.summary = QStringLiteral("slave=%1 function=%2 crc=%3").arg(slave).arg(formatHex(functionCode), crcOk ? QStringLiteral("OK") : QStringLiteral("BAD"));
}

void decodeLidarPayload(RawDecodedRecord& decoded, const QByteArray& payload, quint16 recordType, bool english)
{
    decoded.title = lidarProtocolName(recordType, english);
    const auto bytes = reinterpret_cast<const uint8_t*>(payload.constData());
    if ((recordType == 2 || recordType == 4) && payload.size() >= 5)
    {
        uint8_t sum = 0;
        for (int i = 1; i < 4; ++i)
        {
            sum = static_cast<uint8_t>(sum + bytes[i]);
        }
        const uint8_t checksum = static_cast<uint8_t>(~sum);
        const bool crcOk = checksum == bytes[4];
        const uint32_t distanceCm = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) | (static_cast<uint32_t>(bytes[3]) << 16);
        addField(decoded, QStringLiteral("TFA1500"), QStringLiteral("header"), formatHex(bytes[0]), bytes[0] == 0x5C ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 0, 1, QString(), bytes[0] != 0x5C);
        addField(decoded, QStringLiteral("TFA1500"), QStringLiteral("distance_raw"), QString::number(distanceCm), QString::number(distanceCm / 100.0, 'f', 3), QStringLiteral("m"), 1, 3);
        addField(decoded, QStringLiteral("TFA1500"), QStringLiteral("checksum"), formatHex(bytes[4]), crcOk ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 4, 1, QString(), !crcOk);
        decoded.summary = QStringLiteral("%1 m").arg(QString::number(distanceCm / 100.0, 'f', 3));
    }
    else if (recordType == 3 && payload.size() >= 6)
    {
        const uint8_t payloadLen = bytes[2];
        const int frameLen = 3 + payloadLen + 1;
        uint8_t checksum = 0;
        const int checksumByteCount = std::min<int>(frameLen - 1, static_cast<int>(payload.size()));
        for (int i = 0; i < checksumByteCount; ++i)
        {
            checksum ^= bytes[i];
        }
        const bool crcOk = frameLen <= payload.size() && checksum == bytes[frameLen - 1];
        addField(decoded, QStringLiteral("TFA1500 Low Frequency"), QStringLiteral("header"), formatHex(bytes[0]), bytes[0] == 0x55 ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), 0, 1, QString(), bytes[0] != 0x55);
        addField(decoded, QStringLiteral("TFA1500 Low Frequency"), QStringLiteral("payload_len"), QString::number(payloadLen), QString(), QStringLiteral("bytes"), 2, 1);
        if (payloadLen >= 7 && payload.size() >= 7)
        {
            const uint32_t distanceDm = (static_cast<uint32_t>(bytes[4]) << 16) | (static_cast<uint32_t>(bytes[5]) << 8) | bytes[6];
            addField(decoded, QStringLiteral("TFA1500 Low Frequency"), QStringLiteral("distance_raw"), QString::number(distanceDm), QString::number(distanceDm / 10.0, 'f', 3), QStringLiteral("m"), 4, 3);
            decoded.summary = QStringLiteral("%1 m").arg(QString::number(distanceDm / 10.0, 'f', 3));
        }
        addField(decoded, QStringLiteral("TFA1500 Low Frequency"), QStringLiteral("checksum"), frameLen <= payload.size() ? formatHex(bytes[frameLen - 1]) : QStringLiteral("---"), crcOk ? QStringLiteral("OK") : QStringLiteral("BAD"), QString(), frameLen <= payload.size() ? frameLen - 1 : -1, frameLen <= payload.size() ? 1 : 0, QString(), !crcOk);
    }
    else if (recordType == 5 && payload.size() >= 10)
    {
        const uint16_t distanceA = static_cast<uint16_t>((bytes[3] << 8) | bytes[4]);
        const uint16_t distanceB = static_cast<uint16_t>((bytes[5] << 8) | bytes[6]);
        const bool ok = bytes[0] == 0xAA && bytes[1] == 0xB7 && bytes[2] == 10 && bytes[9] == 0xBB && distanceA == distanceB;
        addField(decoded, QStringLiteral("AA-B7"), QStringLiteral("header"), QStringLiteral("%1 %2").arg(formatHex(bytes[0]), formatHex(bytes[1])), ok ? QStringLiteral("OK") : QStringLiteral("CHECK"), QString(), 0, 2, QString(), !ok);
        addField(decoded, QStringLiteral("AA-B7"), QStringLiteral("distance_a"), QString::number(distanceA), QString::number(distanceA / 100.0, 'f', 3), QStringLiteral("m"), 3, 2);
        addField(decoded, QStringLiteral("AA-B7"), QStringLiteral("distance_b"), QString::number(distanceB), QString::number(distanceB / 100.0, 'f', 3), QStringLiteral("m"), 5, 2, QString(), distanceA != distanceB);
        addField(decoded, QStringLiteral("AA-B7"), QStringLiteral("signal_strength"), QString::number(bytes[8]), QString(), QString(), 8, 1);
        decoded.summary = QStringLiteral("%1 m, strength=%2").arg(QString::number(distanceA / 100.0, 'f', 3)).arg(bytes[8]);
    }
    else
    {
        addField(decoded, QStringLiteral("Lidar"), QStringLiteral("payload"), QString::number(payload.size()), QString(), QStringLiteral("bytes"), 0, payload.size(), QStringLiteral("No matching decoder"), true);
    }
    decoded.status = decoded.ok ? (english ? QStringLiteral("OK") : QStringLiteral("正常")) : (english ? QStringLiteral("Issue") : QStringLiteral("异常"));
    if (decoded.summary.isEmpty())
    {
        decoded.summary = lidarProtocolName(recordType, english);
    }
}

void decodeTcpWavePayload(RawDecodedRecord& decoded, const QByteArray& payload, quint32 flags, bool english)
{
    decoded.title = QStringLiteral("TCP Wave");
    if (payload.size() < 8)
    {
        addField(decoded, QStringLiteral("TCP Wave"), QStringLiteral("payload"), QString::number(payload.size()), QString(), QStringLiteral("bytes"), 0, payload.size(), QStringLiteral("Too short"), true);
        decoded.status = english ? QStringLiteral("Invalid") : QStringLiteral("无效");
        decoded.summary = english ? QStringLiteral("Too short") : QStringLiteral("长度过短");
        return;
    }
    const auto bytes = reinterpret_cast<const uint8_t*>(payload.constData());
    const quint32 rawSize = readU32LE(bytes);
    const quint32 harmonicSize = readU32LE(bytes + 4);
    const quint64 required = 8ULL + rawSize + harmonicSize;
    const bool sizeOk = required <= static_cast<quint64>(payload.size());
    addField(decoded, QStringLiteral("TCP Wave"), QStringLiteral("raw_signal_payload_size"), QString::number(rawSize), QString(), QStringLiteral("bytes"), 0, 4, QString(), !sizeOk);
    addField(decoded, QStringLiteral("TCP Wave"), QStringLiteral("harmonic_payload_size"), QString::number(harmonicSize), QString(), QStringLiteral("bytes"), 4, 4, QString(), !sizeOk);
    const VaporView::TcpFloatEncoding encoding = VaporView::tcpFloatEncodingFromRawDatFlags(flags);
    addField(decoded, QStringLiteral("TCP Wave"), QStringLiteral("float_encoding"), formatHex((flags & VaporView::kTcpWaveFloatEncodingFlagMask) >> VaporView::kTcpWaveFloatEncodingFlagShift), VaporView::tcpFloatEncodingLabel(english, encoding), QString(), -1, 0);
    if (sizeOk && harmonicSize > 0 && harmonicSize % kFloatBytes == 0)
    {
        const QByteArray harmonic = payload.mid(static_cast<int>(8 + rawSize), static_cast<int>(harmonicSize));
        const VaporView::TcpFloatEncoding effectiveEncoding = encoding == VaporView::TcpFloatEncoding::Unknown
            ? VaporView::autoDetectTcpFloatEncoding(harmonic)
            : encoding;
        const QVector<float> samples = VaporView::decodeTcpFloatPayload(harmonic, effectiveEncoding);
        auto minMax = std::minmax_element(samples.cbegin(), samples.cend());
        addField(decoded, QStringLiteral("Harmonic Payload"), QStringLiteral("sample_count"), QString::number(samples.size()), QString(), QStringLiteral("float32"), static_cast<int>(8 + rawSize), static_cast<int>(harmonicSize));
        if (minMax.first != samples.cend())
        {
            addNumericField(decoded, QStringLiteral("Harmonic Payload"), QStringLiteral("min"), *minMax.first, QString(), static_cast<int>(8 + rawSize), static_cast<int>(harmonicSize));
            addNumericField(decoded, QStringLiteral("Harmonic Payload"), QStringLiteral("max"), *minMax.second, QString(), static_cast<int>(8 + rawSize), static_cast<int>(harmonicSize));
        }
        QStringList preview;
        for (int i = 0; i < std::min<int>(static_cast<int>(samples.size()), 8); ++i)
        {
            preview << QString::number(samples.at(i), 'g', 7);
        }
        addField(decoded, QStringLiteral("Harmonic Payload"), QStringLiteral("first_samples"), preview.join(QStringLiteral(", ")), QString(), QString(), static_cast<int>(8 + rawSize), std::min<int>(static_cast<int>(harmonicSize), 32));
        decoded.summary = QStringLiteral("raw=%1 bytes, harmonic=%2 samples").arg(rawSize).arg(samples.size());
    }
    decoded.status = decoded.ok ? (english ? QStringLiteral("OK") : QStringLiteral("正常")) : (english ? QStringLiteral("Issue") : QStringLiteral("异常"));
    if (decoded.summary.isEmpty())
    {
        decoded.summary = QStringLiteral("raw=%1 bytes, harmonic=%2 bytes").arg(rawSize).arg(harmonicSize);
    }
}

RawDecodedRecord decodeRawRecord(const RawRecordIndex& record, const QByteArray& payload, bool english)
{
    RawDecodedRecord decoded;
    decoded.title = recordTypeName(record.source_id, record.record_type, english);
    decoded.status = english ? QStringLiteral("OK") : QStringLiteral("正常");
    decoded.summary = QStringLiteral("%1 bytes").arg(payload.size());
    addUnifiedRecordFields(decoded, record);

    if (payload.size() != static_cast<int>(record.payload_size))
    {
        addField(decoded, QStringLiteral("Payload"), QStringLiteral("read_size"), QString::number(payload.size()), QString::number(record.payload_size), QStringLiteral("expected bytes"), 0, payload.size(), QStringLiteral("Could not read complete payload"), true);
        decoded.status = english ? QStringLiteral("Read issue") : QStringLiteral("读取异常");
        return decoded;
    }

    switch (record.source_id)
    {
    case kRawSourceEpsilon:
        decodeEpsilonPayload(decoded, payload, english);
        break;
    case kRawSourcePtb:
        decodePtbPayload(decoded, payload, english);
        break;
    case kRawSourceHmp:
        decodeHmpPayload(decoded, payload, english);
        break;
    case kRawSourceLidar:
        decodeLidarPayload(decoded, payload, record.record_type, english);
        break;
    case kRawSourceTcpWave:
        decodeTcpWavePayload(decoded, payload, record.flags, english);
        break;
    default:
        addField(decoded, QStringLiteral("Payload"), QStringLiteral("hex_preview"), hexPreview(payload), QString(), QString(), 0, payload.size());
        break;
    }
    return decoded;
}

}  // namespace
