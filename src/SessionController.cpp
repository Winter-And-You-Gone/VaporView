#include "SessionController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QStringConverter>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr quint64 kWaveformTimestampBytes = sizeof(quint64);
constexpr quint64 kFloatBytes = sizeof(float);

QString csvValueAt(const QStringList &fields, int index)
{
    if (index < 0 || index >= fields.size()) {
        return QString();
    }
    return fields.at(index);
}

QString formatTimestampUs(quint64 timestampUs)
{
    if (timestampUs == 0) {
        return QObject::tr("N/A");
    }

    const qint64 millis = static_cast<qint64>(timestampUs / 1000ULL);
    const int micros = static_cast<int>(timestampUs % 1000000ULL);
    return QStringLiteral("%1.%2")
        .arg(QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC).toLocalTime().toString("yyyy-MM-dd HH:mm:ss"))
        .arg(micros, 6, 10, QChar('0'));
}

QStringList parseCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                current += QLatin1Char('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == QLatin1Char(',') && !inQuotes) {
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

SessionController::SessionController(QObject *parent)
    : QObject(parent)
{
    updateFrameInfoForEmptyState();
    updateCsvInfoText();
    setStatusText(textFor("No session loaded.", "当前没有已加载的会话。"));
}

bool SessionController::english() const { return english_; }
QString SessionController::sessionPath() const { return session_directory_; }
QString SessionController::statusText() const { return status_text_; }
QString SessionController::sessionName() const { return session_name_; }
QString SessionController::startTime() const { return start_time_utc_; }
QString SessionController::endTime() const { return end_time_utc_; }
qulonglong SessionController::sensorRows() const { return total_sensor_rows_; }
qulonglong SessionController::waveformFiles() const { return static_cast<qulonglong>(waveform_segments_.size()); }
qulonglong SessionController::waveformFrames() const { return total_waveform_frames_; }
QString SessionController::csvInfoText() const { return csv_info_text_; }
QString SessionController::frameInfoText() const { return frame_info_text_; }
int SessionController::currentFrame() const { return current_frame_; }
int SessionController::totalFrames() const
{
    return static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max())));
}
int SessionController::highlightedRow() const { return highlighted_row_; }
bool SessionController::peakScatterMode() const { return peak_scatter_mode_; }
QVariantList SessionController::csvColumnSource() const { return csv_column_source_; }
QVariantList SessionController::csvRows() const { return csv_rows_; }
QVariantList SessionController::currentWaveSamples() const { return current_wave_samples_; }
QVariantList SessionController::peakValues() const { return peak_values_; }

void SessionController::setEnglish(bool english)
{
    if (english_ == english) {
        return;
    }
    english_ = english;
    emit englishChanged();
    updateCsvInfoText();
    if (current_frame_ > 0) {
        loadWaveformFrame(static_cast<quint64>(current_frame_ - 1));
    } else {
        updateFrameInfoForEmptyState();
        emit waveformChanged();
    }
    if (session_directory_.isEmpty()) {
        setStatusText(textFor("No session loaded.", "当前没有已加载的会话。"));
    }
    emit csvChanged();
}

QString SessionController::textFor(const QString &englishText, const QString &chineseText) const
{
    return english_ ? englishText : chineseText;
}

QString SessionController::localFilePath(const QUrl &url)
{
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    const QString stringValue = url.toString();
    if (stringValue.startsWith(QStringLiteral("file:///"))) {
        return QUrl(stringValue).toLocalFile();
    }
    return stringValue;
}

QString SessionController::resolveSessionDirectory(const QString &path) const
{
    if (path.isEmpty()) {
        return QString();
    }

    const QString normalized = path.startsWith(QStringLiteral("file:")) ? localFilePath(QUrl(path)) : path;
    QFileInfo info(normalized);
    if (info.isDir()) {
        return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
    if (info.isFile() && info.fileName().compare(QStringLiteral("session.json"), Qt::CaseInsensitive) == 0) {
        return QDir::fromNativeSeparators(info.absolutePath());
    }
    return QString();
}

void SessionController::setStatusText(const QString &text)
{
    if (status_text_ != text) {
        status_text_ = text;
        emit statusChanged();
    }
}

bool SessionController::loadSessionPath(const QString &path)
{
    const QString sessionDirectory = resolveSessionDirectory(path);
    if (sessionDirectory.isEmpty()) {
        setStatusText(textFor("The selected path is not a session directory or session.json file.",
                              "选择的路径不是有效的 session 目录或 session.json 文件。"));
        return false;
    }
    return loadSessionDirectory(sessionDirectory);
}

bool SessionController::loadSessionUrl(const QUrl &url)
{
    return loadSessionPath(localFilePath(url));
}

bool SessionController::reload()
{
    if (session_directory_.isEmpty()) {
        setStatusText(textFor("No session is currently loaded.", "当前没有已加载的会话。"));
        return false;
    }
    return loadSessionDirectory(session_directory_);
}

void SessionController::clear()
{
    clearLoadedData(true);
}

void SessionController::setCurrentFrame(int frameNumber)
{
    if (frameNumber <= 0 || total_waveform_frames_ == 0) {
        return;
    }
    if (frameNumber > totalFrames()) {
        frameNumber = totalFrames();
    }
    loadWaveformFrame(static_cast<quint64>(frameNumber - 1));
}

void SessionController::togglePeakPlotMode()
{
    peak_scatter_mode_ = !peak_scatter_mode_;
    emit waveformChanged();
}

bool SessionController::loadSessionDirectory(const QString &sessionDirectory)
{
    clearLoadedData(false);

    const QString normalized = QDir::fromNativeSeparators(sessionDirectory);
    if (!loadSessionMetadata(normalized)) {
        return false;
    }
    if (!loadSensorsCsv()) {
        return false;
    }
    if (!loadWaveformSegments()) {
        return false;
    }
    if (!loadWaveformPeakSeries()) {
        return false;
    }

    session_directory_ = normalized;
    emit sessionChanged();
    emit summaryChanged();
    emit csvChanged();
    emit waveformChanged();

    if (total_waveform_frames_ > 0) {
        loadWaveformFrame(0);
    } else {
        updateFrameInfoForEmptyState();
    }

    setStatusText(QString(textFor("Loaded session: %1", "已加载会话: %1")).arg(session_directory_));
    return true;
}

bool SessionController::loadSessionMetadata(const QString &sessionDirectory)
{
    const QString metadataPath = QDir(sessionDirectory).filePath(QStringLiteral("session.json"));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusText(QString(textFor("Failed to open session.json: %1", "打开 session.json 失败: %1")).arg(metadataPath));
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        setStatusText(QString(textFor("Invalid session metadata: %1", "session 元数据无效: %1")).arg(metadataPath));
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

bool SessionController::loadSensorsCsv()
{
    csv_headers_.clear();
    csv_timestamps_us_.clear();
    csv_rows_.clear();
    csv_column_source_.clear();

    QFile file(sensors_csv_filename_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        updateCsvInfoText();
        setStatusText(QString(textFor("Failed to open sensors CSV: %1", "打开传感器 CSV 失败: %1")).arg(sensors_csv_filename_));
        emit csvChanged();
        return true;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd()) {
        updateCsvInfoText();
        emit csvChanged();
        return true;
    }

    csv_headers_ = parseCsvLine(stream.readLine());
    rebuildColumnSource();

    QVector<QStringList> rows;
    rows.reserve(static_cast<int>(std::min<quint64>(total_sensor_rows_ > 0 ? total_sensor_rows_ : 256ULL, 50000ULL)));
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty()) {
            continue;
        }

        QStringList fields = parseCsvLine(line);
        while (fields.size() < csv_headers_.size()) {
            fields.push_back(QString());
        }
        rows.push_back(fields);

        QVariantMap rowMap;
        for (int col = 0; col < csv_headers_.size(); ++col) {
            rowMap.insert(QStringLiteral("c%1").arg(col), csvValueAt(fields, col));
        }
        csv_rows_.append(rowMap);

        bool ok = false;
        csv_timestamps_us_.push_back(csvValueAt(fields, 0).toULongLong(&ok));
        if (!ok) {
            csv_timestamps_us_.last() = 0;
        }
    }

    total_sensor_rows_ = static_cast<quint64>(rows.size());
    updateCsvInfoText();
    emit csvChanged();
    emit summaryChanged();
    return true;
}

bool SessionController::loadWaveformSegments()
{
    waveform_segments_.clear();
    total_waveform_frames_ = 0;

    QDir dir(waveform_directory_);
    if (!dir.exists()) {
        setStatusText(QString(textFor("Waveform directory does not exist: %1", "波形目录不存在: %1")).arg(waveform_directory_));
        emit summaryChanged();
        return true;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;

    for (const QString &filename : files) {
        const QString absolutePath = dir.filePath(filename);
        const QFileInfo info(absolutePath);
        if (frameBytes == 0 || info.size() < static_cast<qint64>(frameBytes)) {
            continue;
        }

        const quint64 frameCount = static_cast<quint64>(info.size()) / frameBytes;
        if (frameCount == 0) {
            continue;
        }

        WaveformSegment segment;
        segment.filename = absolutePath;
        segment.startFrame = total_waveform_frames_;
        segment.frameCount = frameCount;
        waveform_segments_.push_back(segment);
        total_waveform_frames_ += frameCount;
    }

    emit summaryChanged();
    return true;
}

bool SessionController::loadWaveformPeakSeries()
{
    waveform_peak_values_.clear();
    peak_values_.clear();

    if (waveform_segments_.isEmpty() || points_per_frame_ <= 0) {
        emit waveformChanged();
        return true;
    }

    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;
    QVector<float> frameSamples(points_per_frame_);

    for (const WaveformSegment &segment : waveform_segments_) {
        QFile file(segment.filename);
        if (!file.open(QIODevice::ReadOnly)) {
            setStatusText(QString(textFor("Failed to scan waveform file: %1", "扫描波形文件失败: %1")).arg(segment.filename));
            return false;
        }

        for (quint64 frame = 0; frame < segment.frameCount; ++frame) {
            const QByteArray block = file.read(static_cast<qint64>(frameBytes));
            if (block.size() != static_cast<int>(frameBytes)) {
                setStatusText(QString(textFor("Incomplete waveform frame in %1", "%1 中的波形帧不完整")).arg(segment.filename));
                return false;
            }

            std::memcpy(frameSamples.data(), block.constData() + sizeof(quint64), static_cast<size_t>(points_per_frame_) * sizeof(float));
            const auto peakIt = std::max_element(frameSamples.cbegin(), frameSamples.cend());
            waveform_peak_values_.push_back(peakIt == frameSamples.cend() ? 0.0f : *peakIt);
        }
    }

    peak_values_.reserve(waveform_peak_values_.size());
    for (float value : waveform_peak_values_) {
        peak_values_.append(value);
    }
    emit waveformChanged();
    return true;
}

bool SessionController::loadWaveformFrame(quint64 frameIndex)
{
    if (waveform_segments_.isEmpty() || frameIndex >= total_waveform_frames_) {
        return false;
    }

    const auto it = std::find_if(waveform_segments_.cbegin(), waveform_segments_.cend(), [frameIndex](const WaveformSegment &segment) {
        return frameIndex >= segment.startFrame && frameIndex < segment.startFrame + segment.frameCount;
    });
    if (it == waveform_segments_.cend()) {
        return false;
    }

    const quint64 localFrame = frameIndex - it->startFrame;
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;
    const quint64 offset = localFrame * frameBytes;

    QFile file(it->filename);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(static_cast<qint64>(offset))) {
        setStatusText(QString(textFor("Failed to read waveform file: %1", "读取波形文件失败: %1")).arg(it->filename));
        return false;
    }

    const QByteArray block = file.read(static_cast<qint64>(frameBytes));
    if (block.size() != static_cast<int>(frameBytes)) {
        setStatusText(QString(textFor("Incomplete waveform frame in %1", "%1 中的波形帧不完整")).arg(it->filename));
        return false;
    }

    quint64 timestampUs = 0;
    std::memcpy(&timestampUs, block.constData(), sizeof(quint64));

    QVector<float> samples(points_per_frame_);
    std::memcpy(samples.data(), block.constData() + sizeof(quint64), static_cast<size_t>(points_per_frame_) * sizeof(float));

    current_wave_samples_.clear();
    current_wave_samples_.reserve(samples.size());
    for (float value : samples) {
        current_wave_samples_.append(value);
    }

    const auto minMax = std::minmax_element(samples.cbegin(), samples.cend());
    const float peakValue = frameIndex < static_cast<quint64>(waveform_peak_values_.size())
                                ? waveform_peak_values_.at(static_cast<int>(frameIndex))
                                : (samples.isEmpty() ? 0.0f : *minMax.second);
    frame_info_text_ = QString(textFor("Frame %1 / %2 | %3 | %4 Hz export | min=%5 max=%6 peak=%7 | %8",
                                       "第 %1 / %2 帧 | %3 | %4 Hz 导出 | min=%5 max=%6 峰值=%7 | %8"))
                           .arg(frameIndex + 1)
                           .arg(total_waveform_frames_)
                           .arg(formatTimestampUs(timestampUs))
                           .arg(waveform_export_rate_hz_)
                           .arg(samples.isEmpty() ? QStringLiteral("0") : QString::number(*minMax.first, 'f', 6))
                           .arg(samples.isEmpty() ? QStringLiteral("0") : QString::number(*minMax.second, 'f', 6))
                           .arg(QString::number(peakValue, 'f', 6))
                           .arg(QFileInfo(it->filename).fileName());

    current_frame_ = static_cast<int>(frameIndex + 1);
    highlightClosestSensorRow(timestampUs);
    emit waveformChanged();
    return true;
}

void SessionController::clearLoadedData(bool clearPath)
{
    if (clearPath) {
        session_directory_.clear();
        emit sessionChanged();
    }
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
    csv_column_source_.clear();
    csv_rows_.clear();
    current_wave_samples_.clear();
    peak_values_.clear();
    current_frame_ = 0;
    highlighted_row_ = -1;
    total_sensor_rows_ = 0;
    total_waveform_frames_ = 0;
    points_per_frame_ = 50000;
    waveform_export_rate_hz_ = 10;

    updateCsvInfoText();
    updateFrameInfoForEmptyState();
    emit summaryChanged();
    emit csvChanged();
    emit waveformChanged();
    setStatusText(textFor("The current page has been cleared.", "当前页面内容已清空。"));
}

void SessionController::rebuildColumnSource()
{
    csv_column_source_.clear();
    for (int index = 0; index < csv_headers_.size(); ++index) {
        QVariantMap column;
        column.insert(QStringLiteral("title"), csv_headers_.at(index));
        column.insert(QStringLiteral("dataIndex"), QStringLiteral("c%1").arg(index));
        column.insert(QStringLiteral("minimumWidth"), index == 0 ? 150 : 120);
        column.insert(QStringLiteral("width"), index == 0 ? 180 : 140);
        csv_column_source_.append(column);
    }
}

void SessionController::updateFrameInfoForEmptyState()
{
    frame_info_text_ = textFor("No waveform frame loaded", "尚未加载波形帧");
}

void SessionController::updateCsvInfoText()
{
    if (!sensors_csv_filename_.isEmpty() && !csv_rows_.isEmpty()) {
        csv_info_text_ = QString(textFor("Loaded %1 CSV rows from %2", "已从 %2 加载 %1 行 CSV"))
                             .arg(total_sensor_rows_)
                             .arg(QDir::toNativeSeparators(sensors_csv_filename_));
    } else if (!sensors_csv_filename_.isEmpty()) {
        csv_info_text_ = textFor("The session metadata is valid, but sensors/devices.csv could not be opened.",
                                 "session 元数据是有效的，但 sensors/devices.csv 无法打开。");
    } else {
        csv_info_text_ = textFor("No CSV loaded", "尚未加载 CSV");
    }
}

void SessionController::highlightClosestSensorRow(quint64 timestampUs)
{
    if (csv_timestamps_us_.isEmpty()) {
        highlighted_row_ = -1;
        return;
    }

    const auto it = std::lower_bound(csv_timestamps_us_.cbegin(), csv_timestamps_us_.cend(), timestampUs);
    if (it == csv_timestamps_us_.cend()) {
        highlighted_row_ = csv_timestamps_us_.size() - 1;
        return;
    }
    if (it == csv_timestamps_us_.cbegin()) {
        highlighted_row_ = 0;
        return;
    }

    const int lowerIndex = static_cast<int>(it - csv_timestamps_us_.cbegin());
    const quint64 upper = csv_timestamps_us_.at(lowerIndex);
    const quint64 lower = csv_timestamps_us_.at(lowerIndex - 1);
    highlighted_row_ = (timestampUs - lower <= upper - timestampUs) ? lowerIndex - 1 : lowerIndex;
}
