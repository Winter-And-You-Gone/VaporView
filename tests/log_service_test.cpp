#include "LogService.h"
#include "TelemetryCodec.h"
#include "logging/LogQueuePolicy.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSemaphore>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <iostream>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

struct UnsupportedLogValue
{
    int value = 0;
};

Q_DECLARE_METATYPE(UnsupportedLogValue)

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool waitForFile(const QString& path,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    const QDeadlineTimer deadline(timeout);
    while (!deadline.hasExpired())
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) && !file.readAll().trimmed().isEmpty())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

bool waitForText(const QString& path,
                 const QByteArray& text,
                 std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const QDeadlineTimer deadline(timeout);
    while (!deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) && file.readAll().contains(text))
        {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

bool waitForCondition(const std::function<bool()>& condition,
                      std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    const QDeadlineTimer deadline(timeout);
    while (!deadline.hasExpired())
    {
        if (condition())
        {
            return true;
        }
        QThread::msleep(5);
    }
    return condition();
}

VaporView::LogRecord queueRecord(VaporView::LogLevel level,
                                 quint64 sequence,
                                 const QString& message)
{
    VaporView::LogRecord record;
    record.level = level;
    record.sequence = sequence;
    record.message = message;
    return record;
}

QVector<QJsonObject> readJsonLines(const QString& path)
{
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    require(opened, "open JSONL file for parsing");
    QVector<QJsonObject> records;
    while (!file.atEnd())
    {
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        require(document.isObject(), "every JSONL line is a complete object");
        records.push_back(document.object());
    }
    return records;
}

QJsonObject parseJsonLine(const QByteArray& line)
{
    require(line.size() <= VaporView::LogRecordLimits::kMaxSerializedRecordBytes,
            "serialized record stays within its hard byte limit");
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            "bounded JSONL is a complete parseable object");
    return document.object();
}

void silentQtMessageHandler(QtMsgType, const QMessageLogContext&, const QString&)
{
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary log directory");

    VaporView::LogRecord source;
    source.timestamp_utc = QStringLiteral("2026-07-30T12:00:00.000Z");
    source.timestamp_us = 123456;
    source.level = VaporView::LogLevel::Warning;
    source.source = QStringLiteral("Test");
    source.category = QStringLiteral("logging");
    source.sequence = 7;
    source.message = QStringLiteral("queue warning");
    source.fields.insert(QStringLiteral("dropped_count"), 3);

    const QByteArray payload = VaporView::TelemetryCodec::serializeLogRecord(source);
    VaporView::LogRecord parsed;
    require(VaporView::TelemetryCodec::parseLogRecord(payload, parsed), "log record IPC round trip");
    require(parsed.level == VaporView::LogLevel::Warning, "log level round trip");
    require(parsed.message == source.message, "log message round trip");
    require(parsed.fields.value(QStringLiteral("dropped_count")).toInt() == 3, "log fields round trip");

    // ExplicitLogLevelIsIndependentOfMessageText: the same failure wording may
    // be intentionally emitted at different levels by different call sites.
    {
        VaporView::LogRecord warning = source;
        warning.level = VaporView::LogLevel::Warning;
        warning.message = QStringLiteral("设备连接失败。");
        warning.fields.insert(QStringLiteral("event"), QStringLiteral("device_connection_retry"));
        warning.fields.insert(QStringLiteral("error_code"), QStringLiteral("device_connect_failed"));
        VaporView::LogRecord parsedWarning;
        require(VaporView::TelemetryCodec::parseLogRecord(
                    VaporView::TelemetryCodec::serializeLogRecord(warning), parsedWarning),
                "warning with failure wording round trip");
        require(parsedWarning.level == VaporView::LogLevel::Warning,
                "warning level is not inferred from failure wording");

        VaporView::LogRecord error = warning;
        error.level = VaporView::LogLevel::Error;
        VaporView::LogRecord parsedError;
        require(VaporView::TelemetryCodec::parseLogRecord(
                    VaporView::TelemetryCodec::serializeLogRecord(error), parsedError),
                "error with failure wording round trip");
        require(parsedError.level == VaporView::LogLevel::Error,
                "error level remains the explicit level");
    }

    // ExternalRawErrorRemainsInFields: first-party Chinese context must not
    // replace the original operating-system/device text.
    {
        VaporView::LogRecord record = source;
        record.message = QStringLiteral("串口打开失败。");
        record.fields.clear();
        record.fields.insert(QStringLiteral("event"), QStringLiteral("serial_open_failed"));
        record.fields.insert(QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED"));
        record.fields.insert(QStringLiteral("system_error"), QStringLiteral("Access is denied."));
        record.fields.insert(QStringLiteral("external_raw_text"), QStringLiteral("driver returned EACCES"));
        VaporView::LogRecord parsedExternal;
        require(VaporView::TelemetryCodec::parseLogRecord(
                    VaporView::TelemetryCodec::serializeLogRecord(record), parsedExternal),
                "external raw error record round trip");
        require(parsedExternal.message == QStringLiteral("串口打开失败。"),
                "Chinese first-party message is preserved beside external text");
        require(parsedExternal.fields.value(QStringLiteral("system_error")).toString() ==
                    QStringLiteral("Access is denied."),
                "system error remains in its dedicated field");
        require(parsedExternal.fields.value(QStringLiteral("external_raw_text")).toString() ==
                    QStringLiteral("driver returned EACCES"),
                "external raw text remains in its dedicated field");
    }

    // shortMessageIsUnchanged / longMessageIsUtf8SafelyTruncated /
    // multibyteMessageIsNotCutMidCharacter / messageLimitUsesUtf8BytesNotQStringLength /
    // truncatedMessageContainsMetadata
    {
        VaporView::LogRecord record = source;
        record.message = QStringLiteral("short 中文 emoji 🚀");
        QJsonObject object = parseJsonLine(record.toJsonLine());
        require(object.value(QStringLiteral("message")).toString() == record.message,
                "short message is unchanged");
        require(!object.value(QStringLiteral("fields")).toObject()
                     .contains(QStringLiteral("_log_truncated")),
                "short message has no truncation metadata");

        record.message = QString(VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                                 QLatin1Char('a'));
        object = parseJsonLine(record.toJsonLine());
        require(object.value(QStringLiteral("message")).toString() == record.message,
                "message exactly at the UTF-8 byte limit is unchanged");

        const QString multibyte =
            QString(VaporView::LogRecordLimits::kMaxMessageUtf8Bytes / 3,
                    QChar(0x4E2D)) + QStringLiteral("🚀");
        require(multibyte.size() < VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                "multibyte fixture has fewer UTF-16 code units than the byte limit");
        record.message = multibyte;
        object = parseJsonLine(record.toJsonLine());
        const QString boundedMessage = object.value(QStringLiteral("message")).toString();
        require(boundedMessage.toUtf8().size() <=
                    VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                "message limit is measured in UTF-8 bytes");
        require(QString::fromUtf8(boundedMessage.toUtf8()) == boundedMessage,
                "multibyte message remains valid UTF-8");
        require(boundedMessage.endsWith(QStringLiteral("...<truncated>")),
                "truncated multibyte message has an explicit marker");
        const QJsonObject metadata = object.value(QStringLiteral("fields")).toObject();
        require(metadata.value(QStringLiteral("_log_truncated")).toBool(),
                "truncated message contains metadata");
        require(metadata.value(QStringLiteral("_log_original_message_utf8_bytes")).toDouble() ==
                    multibyte.toUtf8().size(),
                "truncated message metadata preserves original UTF-8 byte count");
        require(metadata.value(QStringLiteral("_log_truncation_reasons")).toArray()
                    .contains(QStringLiteral("message_limit")),
                "truncated message reports the machine-readable reason");
    }

    // largeStringFieldIsTruncated / largeByteArrayFieldIsTruncated /
    // deepVariantIsBounded / largeListIsBounded / largeMapIsBounded /
    // unsupportedVariantTypeDoesNotBreakRecord / fieldsSizeLimitProducesValidJson /
    // fieldTruncationMetadataIsPresent
    {
        VaporView::LogRecord record = source;
        record.message = QStringLiteral("bounded fields");
        record.fields.clear();
        record.fields.insert(QStringLiteral("large_string"),
                             QString(70 * 1024, QLatin1Char('s')));
        record.fields.insert(QStringLiteral("large_bytes"),
                             QByteArray(70 * 1024, 'b'));

        QVariant deepValue = QStringLiteral("leaf");
        for (int depth = 0; depth < 20; ++depth)
        {
            deepValue = QVariantMap{{QStringLiteral("child"), deepValue}};
        }
        record.fields.insert(QStringLiteral("deep"), deepValue);

        QVariantList list;
        for (int index = 0; index < 300; ++index)
        {
            list.push_back(QStringLiteral("item-%1").arg(index));
        }
        record.fields.insert(QStringLiteral("large_list"), list);
        record.fields.insert(QStringLiteral("unsupported"),
                             QVariant::fromValue(UnsupportedLogValue{42}));

        const QJsonObject object = parseJsonLine(record.toJsonLine());
        const QJsonObject fieldsObject = object.value(QStringLiteral("fields")).toObject();
        require(fieldsObject.value(QStringLiteral("large_string")).toString().toUtf8().size() <=
                    VaporView::LogRecordLimits::kMaxSingleStringUtf8Bytes,
                "large string field is bounded");
        require(fieldsObject.value(QStringLiteral("large_string")).toString()
                    .endsWith(QStringLiteral("...<truncated>")),
                "large string field carries a truncation marker");
        require(fieldsObject.value(QStringLiteral("large_bytes")).toString().toUtf8().size() <=
                    VaporView::LogRecordLimits::kMaxByteArrayBytes,
                "large QByteArray field is bounded before JSON conversion");
        require(fieldsObject.value(QStringLiteral("large_list")).toArray().size() <=
                    VaporView::LogRecordLimits::kMaxContainerElements,
                "large list retains at most the configured element count");
        require(fieldsObject.value(QStringLiteral("unsupported")).toObject()
                    .value(QStringLiteral("_unsupported_type")).toString()
                    .contains(QStringLiteral("UnsupportedLogValue")),
                "unsupported QVariant is represented by its safe type descriptor");
        require(fieldsObject.value(QStringLiteral("_log_truncated")).toBool(),
                "field truncation metadata is present");
        require(fieldsObject.value(QStringLiteral("_log_truncated_byte_array_count")).toInt() == 1,
                "byte array truncation count is present");
        require(fieldsObject.value(QStringLiteral("_log_dropped_container_elements")).toInt() > 0,
                "container truncation count is present");
        require(fieldsObject.value(QStringLiteral("_log_truncation_reasons")).toArray()
                    .contains(QStringLiteral("variant_depth_limit")),
                "deep QVariant reports its depth limit");
        require(QJsonDocument(fieldsObject).toJson(QJsonDocument::Compact).size() <=
                    VaporView::LogRecordLimits::kMaxFieldsJsonBytes,
                "fields compact JSON remains within its aggregate limit");
    }

    {
        VaporView::LogRecord record = source;
        record.message = QStringLiteral("large map and fields budget");
        record.fields.clear();
        for (int index = 0; index < 300; ++index)
        {
            record.fields.insert(QStringLiteral("field-%1").arg(index, 3, 10, QLatin1Char('0')),
                                 QString(2048, QLatin1Char('v')));
        }
        record.fields.insert(QStringLiteral("_log_truncated"), false);
        const QJsonObject object = parseJsonLine(record.toJsonLine());
        const QJsonObject fieldsObject = object.value(QStringLiteral("fields")).toObject();
        require(fieldsObject.value(QStringLiteral("_log_truncated")).toBool(),
                "business reserved field cannot override truncation metadata");
        require(fieldsObject.value(QStringLiteral("_log_dropped_field_count")).toInt() > 0,
                "large map reports dropped fields");
        require(fieldsObject.value(QStringLiteral("_log_reserved_field_collision_count")).toInt() == 1,
                "reserved field collision is counted");
        require(QJsonDocument(fieldsObject).toJson(QJsonDocument::Compact).size() <=
                    VaporView::LogRecordLimits::kMaxFieldsJsonBytes,
                "large map remains valid bounded fields JSON");
    }

    {
        QVariantHash hash;
        for (int index = 299; index >= 0; --index)
        {
            hash.insert(QStringLiteral("hash-%1").arg(index, 3, 10, QLatin1Char('0')),
                        index);
        }
        VaporView::LogRecord record = source;
        record.fields = {{QStringLiteral("hash"), hash}};
        const QByteArray firstLine = record.toJsonLine();
        const QByteArray secondLine = record.toJsonLine();
        require(firstLine == secondLine,
                "QVariantHash truncation uses deterministic key ordering");
        const QJsonObject fieldsObject = parseJsonLine(firstLine)
                                             .value(QStringLiteral("fields")).toObject();
        require(fieldsObject.value(QStringLiteral("hash")).toObject().size() <=
                    VaporView::LogRecordLimits::kMaxContainerElements,
                "large QVariantHash retains at most the configured element count");
        require(fieldsObject.value(QStringLiteral("_log_truncation_reasons")).toArray()
                    .contains(QStringLiteral("container_element_limit")),
                "large QVariantHash reports its element limit");
    }

    {
        QVariantList leafContainers;
        for (int index = 0; index < 256; ++index)
        {
            leafContainers.push_back(QVariantList());
        }
        QVariantList wideTree;
        for (int index = 0; index < 20; ++index)
        {
            wideTree.push_back(leafContainers);
        }
        VaporView::LogRecord record = source;
        record.fields = {{QStringLiteral("wide_tree"), wideTree}};
        const QJsonObject fieldsObject = parseJsonLine(record.toJsonLine())
                                             .value(QStringLiteral("fields")).toObject();
        require(fieldsObject.value(QStringLiteral("_log_truncation_reasons")).toArray()
                    .contains(QStringLiteral("variant_node_limit")),
                "wide QVariant tree reports the global node-work limit");
    }

    // serializedRecordNeverExceedsHardLimit / oversizedRecordRemainsValidJson /
    // minimumDiagnosticFieldsArePreserved
    {
        VaporView::LogRecord record = source;
        record.timestamp_utc = QString(100 * 1024, QLatin1Char('t'));
        record.source = QString(100 * 1024, QLatin1Char('s'));
        record.category = QString(100 * 1024, QLatin1Char('c'));
        record.correlation_id = QString(100 * 1024, QLatin1Char('r'));
        record.session_id = QString(100 * 1024, QLatin1Char('i'));
        record.message = QString(1024 * 1024, QChar(0x4E2D));
        for (int index = 0; index < 16; ++index)
        {
            record.fields.insert(QStringLiteral("payload-%1").arg(index),
                                 QString(64 * 1024, QLatin1Char('p')));
        }
        const QByteArray line = record.toJsonLine();
        const QJsonObject object = parseJsonLine(line);
        for (const QString& key : {QStringLiteral("schema_version"),
                                   QStringLiteral("timestamp_utc"),
                                   QStringLiteral("timestamp_us"),
                                   QStringLiteral("sequence"),
                                   QStringLiteral("level"),
                                   QStringLiteral("source"),
                                   QStringLiteral("category"),
                                   QStringLiteral("message"),
                                   QStringLiteral("process_id"),
                                   QStringLiteral("thread_id"),
                                   QStringLiteral("fields")})
        {
            require(object.contains(key), "minimum diagnostic field is preserved");
        }
        require(object.value(QStringLiteral("fields")).toObject()
                    .value(QStringLiteral("_log_truncated")).toBool(),
                "whole-record truncation is explicit");
        require(object.value(QStringLiteral("fields")).toObject()
                    .value(QStringLiteral("_log_truncation_reasons")).toArray()
                    .contains(QStringLiteral("record_size_limit")),
                "whole-record size reason is machine-readable");
    }

    using VaporView::LoggingInternal::LogRecordQueue;
    using VaporView::LoggingInternal::QueueEnqueueAction;

    // queueOverflowDropsDebugBeforeInfo / queueOverflowPreservesHighPriority
    {
        LogRecordQueue queue(4, 4);
        queue.enqueue({queueRecord(VaporView::LogLevel::Info, 1, QStringLiteral("info-old")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Warning, 2, QStringLiteral("warning")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Debug, 3, QStringLiteral("debug")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Info, 4, QStringLiteral("info-new")), {}});
        const auto result = queue.enqueue(
            {queueRecord(VaporView::LogLevel::Error, 5, QStringLiteral("error")), {}});
        require(result.action == QueueEnqueueAction::Enqueued,
                "high-priority record is accepted on overflow");
        require(result.evicted && result.evicted->record.message == QStringLiteral("debug"),
                "queue overflow drops Debug before Info regardless of FIFO position");
        require(result.dropped_increment == 1,
                "evicting a queued low-priority record increments dropped count");

        QStringList remainingMessages;
        while (const auto item = queue.takeNext())
        {
            remainingMessages.push_back(item->record.message);
            queue.markProcessed(item->record.level);
        }
        require(remainingMessages == QStringList({QStringLiteral("info-old"),
                                                  QStringLiteral("warning"),
                                                  QStringLiteral("info-new"),
                                                  QStringLiteral("error")}),
                "eviction preserves FIFO order of retained records");
    }

    {
        LogRecordQueue queue(3, 3);
        queue.enqueue({queueRecord(VaporView::LogLevel::Warning, 1, QStringLiteral("w")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Error, 2, QStringLiteral("e")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Critical, 3, QStringLiteral("c")), {}});
        const auto result = queue.enqueue(
            {queueRecord(VaporView::LogLevel::Info, 4, QStringLiteral("i")), {}});
        require(result.action == QueueEnqueueAction::DroppedIncoming,
                "incoming Info is dropped when the queue is all high priority");
        require(result.dropped_increment == 1, "dropped count remains accurate");
        require(queue.size() == 3, "high-priority records remain queued");
    }

    // queuePolicyDoesNotRequireFullScan
    {
        constexpr qsizetype kCapacity = 128;
        constexpr int kSubmissions = 50000;
        LogRecordQueue queue(kCapacity, 8);
        for (qsizetype index = 0; index < kCapacity; ++index)
        {
            const auto level = index % 2 == 0
                ? VaporView::LogLevel::Info
                : VaporView::LogLevel::Debug;
            queue.enqueue({queueRecord(level, static_cast<quint64>(index + 1),
                                       QStringLiteral("seed")), {}});
        }
        quint64 dropped = 0;
        for (int index = 0; index < kSubmissions; ++index)
        {
            const auto result = queue.enqueue(
                {queueRecord(VaporView::LogLevel::Debug,
                             static_cast<quint64>(kCapacity + index + 1),
                             QStringLiteral("stress")), {}});
            dropped += result.dropped_increment;
        }
        const auto stats = queue.stats();
        require(queue.size() == kCapacity, "stress queue remains at its configured capacity");
        require(dropped == kSubmissions, "stress overload dropped count is exact");
        require(stats.priority_index_lookups <= static_cast<quint64>(kSubmissions * 2),
                "overload uses bounded priority-index lookups instead of full scans");
    }

    // criticalQueueHasHardBound / criticalOverflowStateResetsAfterRecovery
    {
        LogRecordQueue queue(2, 2);
        queue.enqueue({queueRecord(VaporView::LogLevel::Warning, 1, QStringLiteral("w")), {}});
        queue.enqueue({queueRecord(VaporView::LogLevel::Error, 2, QStringLiteral("e")), {}});
        require(queue.enqueue({queueRecord(VaporView::LogLevel::Critical, 3,
                                           QStringLiteral("c1")), {}}).action ==
                    QueueEnqueueAction::Enqueued,
                "first pending Critical may exceed normal capacity");
        require(queue.enqueue({queueRecord(VaporView::LogLevel::Critical, 4,
                                           QStringLiteral("c2")), {}}).action ==
                    QueueEnqueueAction::Enqueued,
                "Critical remains FIFO below its pending limit");
        const auto overflow = queue.enqueue(
            {queueRecord(VaporView::LogLevel::Critical, 5, QStringLiteral("c3")), {}});
        require(overflow.action == QueueEnqueueAction::EmergencyWrite,
                "Critical above its pending limit uses emergency output");
        require(overflow.critical_overload_started,
                "first Critical overflow starts one overload cycle");
        require(queue.stats().max_observed_size == 4,
                "Critical queue has a hard capacity plus pending-Critical bound");

        while (const auto item = queue.takeNext())
        {
            queue.markProcessed(item->record.level);
        }
        require(!queue.stats().critical_overload_active,
                "Critical overload state resets after queued Critical recovery");
    }

    const VaporView::LogRecord dropNotice = VaporView::LoggingInternal::makeDropNotice(
        7, 91, 1234567, QStringLiteral("2026-07-31T00:00:00.000Z"), 12, 34);
    require(dropNotice.level == VaporView::LogLevel::Warning,
            "drop notice is warning level");
    require(dropNotice.sequence == 91,
            "drop notice uses its own assigned sequence");
    require(dropNotice.fields.value(QStringLiteral("dropped_count")).toULongLong() == 7,
            "drop notice preserves the aggregated drop count");

    {
        VaporView::LogService service(QStringLiteral("VaporViewTest"), &app, directory.path());
        const VaporView::LogRecord first = service.publish(VaporView::LogLevel::Info,
                                                            QStringLiteral("Test"),
                                                            QStringLiteral("sequence"),
                                                            QStringLiteral("first"));
        const VaporView::LogRecord second = service.publish(VaporView::LogLevel::Info,
                                                             QStringLiteral("Test"),
                                                             QStringLiteral("sequence"),
                                                             QStringLiteral("second"));
        require(second.sequence > first.sequence, "sequence is monotonic");
        service.publish(source);
        service.installQtMessageHandler();
        qWarning("qt handler test message");
        require(waitForFile(service.logFilePath()), "JSONL file is written");

        QFile file(service.logFilePath());
        const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
        require(opened, "open JSONL file");
        bool foundSourceRecord = false;
        while (!file.atEnd())
        {
            const QJsonDocument document = QJsonDocument::fromJson(file.readLine());
            require(document.isObject(), "JSONL line is an object");
            const QJsonObject object = document.object();
            require(object.value(QStringLiteral("schema_version")).toInt() == 1,
                    "JSONL schema version");
            if (object.value(QStringLiteral("message")).toString() == source.message)
            {
                foundSourceRecord = true;
            }
        }
        require(foundSourceRecord, "JSONL preserves message");
        file.seek(0);
        require(QString::fromUtf8(file.readAll()).contains(QStringLiteral("qt handler test message")),
                "Qt message handler writes JSONL");

        service.publish(VaporView::LogLevel::Critical,
                        QStringLiteral("Test"),
                        QStringLiteral("critical"),
                        QStringLiteral("critical-sync"));
        require(waitForText(service.logFilePath(),
                            QByteArrayLiteral("critical-sync"),
                            std::chrono::seconds(1)),
                "critical record is synchronously written");

#ifdef Q_OS_WIN
        QProcess process;
        VaporView::attachProcessLogging(&process,
                                        QStringLiteral("TestProcess"),
                                        QStringLiteral("child"));
        process.start(QStringLiteral("cmd.exe"),
                      {QStringLiteral("/d"),
                       QStringLiteral("/s"),
                       QStringLiteral("/c"),
                       QStringLiteral("echo process-stdout-1 & echo process-stdout-2 & echo process-stderr 1>&2")});
        require(process.waitForStarted(), "child process starts");
        require(process.waitForFinished(10000), "child process finishes");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        require(VaporView::processLoggedStandardOutput(&process).contains("process-stdout"),
                "captured stdout remains available to the process owner");
        require(VaporView::processLoggedStandardError(&process).contains("process-stderr"),
                "captured stderr remains available to the process owner");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stdout")),
                "child stdout is captured");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stdout-2")),
                "child stdout is split into lines");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stderr")),
                "child stderr is captured");

        QProcess largeProcess;
        VaporView::attachProcessLogging(&largeProcess,
                                        QStringLiteral("TestProcess"),
                                        QStringLiteral("child.large"));
        largeProcess.start(QStringLiteral("cmd.exe"),
                           {QStringLiteral("/d"),
                            QStringLiteral("/s"),
                            QStringLiteral("/c"),
                            QStringLiteral("for /L %i in (1,1,70000) do @echo 01234567890123456789")});
        require(largeProcess.waitForStarted(), "large-output child process starts");
        require(largeProcess.waitForFinished(15000), "large-output child process finishes");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        require(VaporView::processLoggedStandardOutput(&largeProcess).size() <= 1024 * 1024,
                "child stdout capture is bounded");
#endif
    }

    // idleInfoLogIsFlushedWithoutAnotherRecord
    {
        QTemporaryDir idleDirectory;
        require(idleDirectory.isValid(), "idle flush temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewIdleFlushTest"),
                                      &app,
                                      idleDirectory.path());
        require(waitForText(service.logFilePath(), QByteArrayLiteral("应用日志系统已启动。")),
                "initial lifecycle log is flushed");
        service.publish(VaporView::LogLevel::Info,
                        QStringLiteral("Test"),
                        QStringLiteral("idle.flush"),
                        QStringLiteral("idle-info-sentinel"));
        require(waitForText(service.logFilePath(),
                            QByteArrayLiteral("idle-info-sentinel"),
                            std::chrono::seconds(2)),
                "idle Info log is flushed while LogService is still alive");
    }

    // criticalFlushPreservesQueuedOrder
    {
        QTemporaryDir fifoDirectory;
        require(fifoDirectory.isValid(), "critical FIFO temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewCriticalFifoTest"),
                                      &app,
                                      fifoDirectory.path());
        require(waitForText(service.logFilePath(), QByteArrayLiteral("应用日志系统已启动。")),
                "critical FIFO lifecycle log is flushed");
        QVector<quint64> acceptedSequences;
        for (int index = 0; index < 128; ++index)
        {
            acceptedSequences.push_back(service.publish(
                VaporView::LogLevel::Info,
                QStringLiteral("Test"),
                QStringLiteral("critical.fifo"),
                QStringLiteral("fifo-info-%1").arg(index)).sequence);
        }
        const VaporView::LogRecord critical = service.publish(
            VaporView::LogLevel::Critical,
            QStringLiteral("Test"),
            QStringLiteral("critical.fifo"),
            QStringLiteral("fifo-critical-sentinel"));

        const QVector<QJsonObject> records = readJsonLines(service.logFilePath());
        quint64 previousSequence = 0;
        int infoRecords = 0;
        bool foundCritical = false;
        for (const QJsonObject& object : records)
        {
            const quint64 sequence = object.value(QStringLiteral("sequence")).toVariant().toULongLong();
            require(sequence > previousSequence, "critical flush preserves monotonic file sequence order");
            previousSequence = sequence;
            const QString message = object.value(QStringLiteral("message")).toString();
            if (message.startsWith(QStringLiteral("fifo-info-")))
            {
                ++infoRecords;
            }
            if (message == QStringLiteral("fifo-critical-sentinel"))
            {
                foundCritical = true;
                require(sequence == critical.sequence, "critical JSON record preserves its sequence");
            }
        }
        require(infoRecords == acceptedSequences.size(),
                "all Info records accepted before Critical are persisted first");
        require(foundCritical, "Critical is persisted before publish returns");
    }

    // fallbackWorksWhenOverridePathIsAFile / fallbackAccessorsAreCrossPlatform /
    // criticalWorksAfterFallback
    {
        QTemporaryDir primaryRoot;
        QTemporaryDir fallbackRoot;
        require(primaryRoot.isValid() && fallbackRoot.isValid(),
                "cross-platform fallback temporary roots");
        const QString blockedPath = QDir(primaryRoot.path()).filePath(QStringLiteral("blocked"));
        QFile blocker(blockedPath);
        const bool blockerOpened = blocker.open(QIODevice::WriteOnly | QIODevice::Truncate);
        require(blockerOpened, "create blocked log path");
        blocker.close();
        const QString fallbackDirectory = QDir(fallbackRoot.path()).filePath(QStringLiteral("logs"));
        {
            VaporView::LogService service(QStringLiteral("VaporViewFallbackTest"),
                                           &app,
                                           blockedPath + QStringLiteral("/logs"),
                                           fallbackDirectory);
            service.publish(VaporView::LogLevel::Info,
                            QStringLiteral("Test"),
                            QStringLiteral("fallback"),
                            QStringLiteral("fallback-info-sentinel"));
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("Test"),
                            QStringLiteral("fallback"),
                            QStringLiteral("fallback-accessor-sentinel"));
            require(QDir::cleanPath(service.logDirectory()) == QDir::cleanPath(fallbackDirectory),
                    "logDirectory reports the active fallback directory");
            require(QFileInfo(service.logFilePath()).absolutePath() ==
                        QFileInfo(fallbackDirectory).absoluteFilePath(),
                    "logFilePath reports a file inside the active fallback directory");
            require(QFileInfo::exists(service.logFilePath()),
                    "fallback path accessor points to the real JSONL file");
            require(waitForText(service.logFilePath(), QByteArrayLiteral("fallback-accessor-sentinel")),
                    "Critical is persisted after switching to fallback");
            require(waitForText(service.logFilePath(), QByteArrayLiteral("fallback-info-sentinel")),
                    "ordinary log is persisted after switching to fallback");
        }
    }

    // criticalOverflowUsesEmergencyPath / criticalOverflowDoesNotDeadlock /
    // emergencyCriticalJsonIsValid / criticalOverflowStateResetsAfterRecovery
    {
        QTemporaryDir criticalDirectory;
        require(criticalDirectory.isValid(), "Critical overload temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewCriticalBoundTest"),
                                      &app,
                                      criticalDirectory.path(),
                                      QDir(criticalDirectory.path()).filePath(QStringLiteral("fallback")));
        require(waitForText(service.logFilePath(),
                            QByteArrayLiteral("应用日志系统已启动。")),
                "Critical overload lifecycle record is flushed");
        require(waitForCondition([&service]() {
                    return service.writerStateForTest()
                               .value(QStringLiteral("size")).toLongLong() == 0;
                }),
                "writer is idle before configuring Critical test limit");
        require(service.setMaxPendingCriticalForTest(4),
                "test-only pending Critical limit is configured while idle");

        auto runOverloadCycle = [&service](const QString& cycleName) {
            service.setWriterBlockedForTest(true);
            require(service.waitForWriterBlockedForTest(std::chrono::seconds(2)),
                    "writer enters the controlled blocked state");

            std::vector<std::thread> criticalWorkers;
            for (int index = 0; index < 4; ++index)
            {
                criticalWorkers.emplace_back([&service, cycleName, index]() {
                    service.publish(VaporView::LogLevel::Critical,
                                    QStringLiteral("CriticalStress"),
                                    QStringLiteral("critical.bound"),
                                    QStringLiteral("%1-queued-%2").arg(cycleName).arg(index));
                });
            }
            require(waitForCondition([&service]() {
                        return service.writerStateForTest()
                                   .value(QStringLiteral("pending_critical")).toLongLong() == 4;
                    }),
                    "controlled writer accumulates exactly the Critical hard limit");

            QElapsedTimer emergencyTimer;
            emergencyTimer.start();
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("CriticalStress"),
                            QStringLiteral("critical.emergency"),
                            cycleName + QStringLiteral("-emergency"));
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("CriticalStress"),
                            QStringLiteral("critical.emergency"),
                            cycleName + QStringLiteral("-emergency-second"));
            require(emergencyTimer.elapsed() < 2000,
                    "Critical emergency writes return within the explicit timeout");

            const QVariantMap blockedState = service.writerStateForTest();
            require(blockedState.value(QStringLiteral("pending_critical")).toLongLong() == 4,
                    "Critical pending count never exceeds its hard limit");
            require(blockedState.value(QStringLiteral("max_observed_size")).toLongLong() <= 4,
                    "controlled Critical queue memory remains bounded");

            service.setWriterBlockedForTest(false);
            for (std::thread& worker : criticalWorkers)
            {
                worker.join();
            }
            require(waitForCondition([&service]() {
                        const QVariantMap state = service.writerStateForTest();
                        return state.value(QStringLiteral("pending_critical")).toLongLong() == 0 &&
                            !state.value(QStringLiteral("critical_overload_active")).toBool();
                    }),
                    "writer recovery clears pending Critical and overload state");
        };

        runOverloadCycle(QStringLiteral("cycle-one"));
        const QString emergencyPath = service.emergencyLogFilePathForTest();
        require(!emergencyPath.isEmpty() && QFileInfo::exists(emergencyPath),
                "Critical overflow creates an independent emergency JSONL file");
        require(waitForText(emergencyPath, QByteArrayLiteral("cycle-one-emergency")),
                "overflow Critical is present in emergency JSONL");

        runOverloadCycle(QStringLiteral("cycle-two"));
        int overloadNotices = 0;
        int emergencyRecords = 0;
        QSet<quint64> emergencySequences;
        for (const QJsonObject& object : readJsonLines(emergencyPath))
        {
            const quint64 sequence = object.value(QStringLiteral("sequence"))
                                         .toVariant().toULongLong();
            require(sequence > 0 && !emergencySequences.contains(sequence),
                    "emergency JSON records preserve unique sequence values");
            emergencySequences.insert(sequence);
            if (object.value(QStringLiteral("category")).toString() ==
                QStringLiteral("queue.critical_overload"))
            {
                ++overloadNotices;
            }
            if (object.value(QStringLiteral("category")).toString() ==
                QStringLiteral("critical.emergency"))
            {
                ++emergencyRecords;
            }
        }
        require(overloadNotices == 2,
                "each recovered Critical overload cycle emits one status record");
        require(emergencyRecords == 4,
                "every overflow Critical is retained as complete emergency JSON");
    }

    // normalAndEmergencyUseSameBoundedSerialization /
    // oversizedCriticalDoesNotCreateHugeEmergencyFile
    {
        VaporView::LogRecord oversized;
        oversized.level = VaporView::LogLevel::Critical;
        oversized.source = QStringLiteral("BoundedCritical");
        oversized.category = QStringLiteral("critical.bounded.serialization");
        oversized.message = QString(2 * 1024 * 1024, QChar(0x4E2D));
        for (int index = 0; index < 8; ++index)
        {
            oversized.fields.insert(QStringLiteral("large-%1").arg(index),
                                    QString(128 * 1024, QLatin1Char('f')));
        }

        QJsonObject normalObject;
        {
            QTemporaryDir normalDirectory;
            require(normalDirectory.isValid(), "normal bounded Critical temporary directory");
            VaporView::LogService service(QStringLiteral("VaporViewBoundedNormalTest"),
                                          &app,
                                          normalDirectory.path());
            const VaporView::LogRecord normal = service.publish(oversized.level,
                                                                 oversized.source,
                                                                 oversized.category,
                                                                 oversized.message,
                                                                 oversized.fields);
            require(normal.message.toUtf8().size() <=
                        VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                    "normal queue receives the bounded message");
            for (const QJsonObject& object : readJsonLines(service.logFilePath()))
            {
                if (object.value(QStringLiteral("category")).toString() == oversized.category)
                {
                    normalObject = object;
                }
            }
            require(!normalObject.isEmpty(), "normal bounded Critical reaches the main JSONL");
        }

        QTemporaryDir emergencyDirectory;
        require(emergencyDirectory.isValid(), "emergency bounded Critical temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewBoundedEmergencyTest"),
                                      &app,
                                      emergencyDirectory.path());
        require(waitForText(service.logFilePath(),
                            QByteArrayLiteral("应用日志系统已启动。")),
                "bounded emergency writer is initially idle");
        require(waitForCondition([&service]() {
                    return service.writerStateForTest().value(QStringLiteral("size")).toInt() == 0;
                }),
                "bounded emergency queue drains before blocking");
        require(service.setMaxPendingCriticalForTest(1),
                "bounded emergency test sets one pending Critical slot");
        service.setWriterBlockedForTest(true);
        require(service.waitForWriterBlockedForTest(std::chrono::seconds(2)),
                "bounded emergency writer enters controlled blocked state");

        std::thread queuedCritical([&service]() {
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("BoundedCritical"),
                            QStringLiteral("critical.bounded.queue"),
                            QStringLiteral("queued-before-bounded-emergency"));
        });
        require(waitForCondition([&service]() {
                    return service.writerStateForTest()
                               .value(QStringLiteral("pending_critical")).toInt() == 1;
                }),
                "one Critical occupies the controlled pending slot");

        QElapsedTimer emergencyTimer;
        emergencyTimer.start();
        const VaporView::LogRecord emergency = service.publish(oversized.level,
                                                                oversized.source,
                                                                oversized.category,
                                                                oversized.message,
                                                                oversized.fields);
        require(emergencyTimer.elapsed() < 5000,
                "oversized emergency Critical completes within an explicit timeout");
        const QString emergencyPath = service.emergencyLogFilePathForTest();
        require(QFileInfo::exists(emergencyPath),
                "oversized Critical creates the independent emergency JSONL");
        service.setWriterBlockedForTest(false);
        queuedCritical.join();

        QJsonObject emergencyObject;
        QFile emergencyFile(emergencyPath);
        const bool emergencyOpened = emergencyFile.open(QIODevice::ReadOnly | QIODevice::Text);
        require(emergencyOpened,
                "open bounded emergency JSONL");
        while (!emergencyFile.atEnd())
        {
            const QByteArray line = emergencyFile.readLine();
            const QJsonObject object = parseJsonLine(line);
            if (object.value(QStringLiteral("category")).toString() == oversized.category)
            {
                emergencyObject = object;
            }
        }
        require(!emergencyObject.isEmpty(),
                "oversized Critical remains a complete emergency JSON record");
        require(emergencyObject.value(QStringLiteral("message")) ==
                    normalObject.value(QStringLiteral("message")),
                "normal and emergency paths use the same bounded message");
        require(emergencyObject.value(QStringLiteral("fields")) ==
                    normalObject.value(QStringLiteral("fields")),
                "normal and emergency paths use the same bounded fields");
        require(QFileInfo(emergencyPath).size() <=
                    2 * VaporView::LogRecordLimits::kMaxSerializedRecordBytes,
                "oversized Critical cannot create a huge emergency file");
        require(emergency.sequence > 0 && emergency.level == VaporView::LogLevel::Critical,
                "bounded emergency preserves sequence and Critical level");
    }

    // fatalMessageIsPersistedBeforeAbort
    {
        QTemporaryDir fatalDirectory;
        require(fatalDirectory.isValid(), "Fatal helper temporary directory");
        QString helperPath = QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("log_fatal_helper"));
#ifdef Q_OS_WIN
        helperPath += QStringLiteral(".exe");
#endif
        require(QFileInfo::exists(helperPath), "Fatal helper executable exists");
        QProcess fatalProcess;
        fatalProcess.start(helperPath, {fatalDirectory.path()});
        require(fatalProcess.waitForStarted(5000), "Fatal helper starts");
        require(fatalProcess.waitForFinished(10000), "Fatal helper terminates within timeout");
        require(fatalProcess.exitStatus() == QProcess::CrashExit || fatalProcess.exitCode() != 0,
                "Fatal helper exits abnormally");

        const QFileInfoList fatalFiles = QDir(fatalDirectory.path()).entryInfoList(
            {QStringLiteral("VaporViewFatalTest-*.jsonl")}, QDir::Files, QDir::Name);
        require(fatalFiles.size() == 1, "Fatal helper creates one JSONL file");
        bool foundFatal = false;
        for (const QJsonObject& object : readJsonLines(fatalFiles.constFirst().absoluteFilePath()))
        {
            if (object.value(QStringLiteral("message")).toString() ==
                QStringLiteral("fatal-persistence-sentinel"))
            {
                foundFatal = true;
                require(object.value(QStringLiteral("level")).toString() == QStringLiteral("Critical"),
                        "Fatal message is persisted as a Critical JSON record");
            }
        }
        require(foundFatal, "Fatal JSON record is persisted before abort");
    }

    // concurrentPublishProducesValidJsonLines
    {
        QTemporaryDir concurrentDirectory;
        require(concurrentDirectory.isValid(), "concurrent publish temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewConcurrentTest"),
                                      &app,
                                      concurrentDirectory.path());
        constexpr int kThreadCount = 4;
        constexpr int kRecordsPerThread = 500;
        std::vector<std::thread> workers;
        workers.reserve(kThreadCount);
        for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
        {
            workers.emplace_back([&service, threadIndex]() {
                for (int recordIndex = 0; recordIndex < kRecordsPerThread; ++recordIndex)
                {
                    service.publish(VaporView::LogLevel::Info,
                                    QStringLiteral("ConcurrentTest"),
                                    QStringLiteral("concurrent.publish"),
                                    QStringLiteral("thread-%1-record-%2")
                                        .arg(threadIndex)
                                        .arg(recordIndex));
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        service.publish(VaporView::LogLevel::Critical,
                        QStringLiteral("ConcurrentTest"),
                        QStringLiteral("concurrent.publish"),
                        QStringLiteral("concurrent-publish-sentinel"));
        int concurrentRecords = 0;
        quint64 previousSequence = 0;
        QSet<quint64> sequences;
        for (const QJsonObject& object : readJsonLines(service.logFilePath()))
        {
            const quint64 sequence = object.value(QStringLiteral("sequence"))
                                         .toVariant().toULongLong();
            require(sequence > previousSequence,
                    "normal concurrent JSONL remains monotonic by sequence");
            require(!sequences.contains(sequence),
                    "normal concurrent JSONL sequence values are unique");
            previousSequence = sequence;
            sequences.insert(sequence);
            if (object.value(QStringLiteral("category")).toString() ==
                QStringLiteral("concurrent.publish") &&
                object.value(QStringLiteral("message")).toString() !=
                    QStringLiteral("concurrent-publish-sentinel"))
            {
                ++concurrentRecords;
            }
        }
        require(concurrentRecords == kThreadCount * kRecordsPerThread,
                "concurrent publish produces complete parseable JSONL records");
    }

    // concurrentQtLoggingDuringShutdownIsSafe
    {
        QtMessageHandler originalHandler = qInstallMessageHandler(&silentQtMessageHandler);
        for (int round = 0; round < 6; ++round)
        {
            QTemporaryDir shutdownDirectory;
            require(shutdownDirectory.isValid(), "concurrent shutdown temporary directory");
            auto service = std::make_unique<VaporView::LogService>(
                QStringLiteral("VaporViewShutdownTest%1").arg(round),
                nullptr,
                shutdownDirectory.path());
            service->installQtMessageHandler();

            std::atomic_bool start = false;
            std::atomic_bool stop = false;
            std::atomic_int ready = 0;
            std::atomic_int published = 0;
            std::vector<std::thread> workers;
            for (int index = 0; index < 4; ++index)
            {
                workers.emplace_back([&]() {
                    ready.fetch_add(1, std::memory_order_release);
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    while (!stop.load(std::memory_order_acquire))
                    {
                        qInfo("shutdown-race-message");
                        published.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            while (ready.load(std::memory_order_acquire) != 4)
            {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);
            QElapsedTimer overlapTimer;
            overlapTimer.start();
            while (published.load(std::memory_order_acquire) < 100 && overlapTimer.elapsed() < 2000)
            {
                QThread::msleep(1);
            }
            require(published.load(std::memory_order_acquire) >= 100,
                    "Qt logging overlaps controlled shutdown");
            service.reset();
            stop.store(true, std::memory_order_release);
            for (std::thread& worker : workers)
            {
                worker.join();
            }
        }
        qInstallMessageHandler(originalHandler);
    }

    // directPublishDuringShutdownIsSafe / newAccessIsRejectedAfterShutdownBegins /
    // activeAccessCompletesBeforeDestruction
    {
        QTemporaryDir shutdownDirectory;
        require(shutdownDirectory.isValid(), "direct publish shutdown temporary directory");
        auto service = std::make_unique<VaporView::LogService>(
            QStringLiteral("VaporViewDirectShutdownTest"),
            nullptr,
            shutdownDirectory.path());
        require(waitForText(service->logFilePath(),
                            QByteArrayLiteral("应用日志系统已启动。")),
                "direct shutdown lifecycle record is flushed");

        QSemaphore accessEntered;
        QSemaphore allowPublish;
        std::atomic_bool activePublishCompleted = false;
        std::atomic_bool workerAccessed = false;
        std::atomic_bool workerReleased = false;
        std::atomic_bool shutdownGateObserved = false;
        std::atomic_bool newAccessRejected = false;

        std::thread activeWorker([&]() {
            const bool accessed = VaporView::LogService::withCurrentInstance(
                [&](VaporView::LogService& activeService) {
                    accessEntered.release();
                    const bool released = allowPublish.tryAcquire(1, 3000);
                    workerReleased.store(released, std::memory_order_release);
                    if (released)
                    {
                        activeService.publish(VaporView::LogLevel::Info,
                                              QStringLiteral("ShutdownTest"),
                                              QStringLiteral("lifecycle.direct"),
                                              QStringLiteral("direct-publish-during-shutdown"));
                        activePublishCompleted.store(true, std::memory_order_release);
                    }
                });
            workerAccessed.store(accessed, std::memory_order_release);
        });
        require(accessEntered.tryAcquire(1, 3000),
                "worker enters lifecycle-protected direct access");

        std::thread shutdownCoordinator([&]() {
            shutdownGateObserved.store(
                VaporView::LogService::waitUntilGlobalAccessRejectedForTest(
                    std::chrono::seconds(3)),
                std::memory_order_release);
            newAccessRejected.store(
                !VaporView::LogService::withCurrentInstance([](VaporView::LogService&) {}),
                std::memory_order_release);
            allowPublish.release();
        });

        service.reset();
        activeWorker.join();
        shutdownCoordinator.join();
        require(shutdownGateObserved.load(std::memory_order_acquire),
                "destructor closes the global access gate before waiting");
        require(workerAccessed.load(std::memory_order_acquire),
                "worker obtains the active service before shutdown");
        require(workerReleased.load(std::memory_order_acquire),
                "active access is released before its timeout");
        require(newAccessRejected.load(std::memory_order_acquire),
                "new safe access is rejected after shutdown begins");
        require(activePublishCompleted.load(std::memory_order_acquire),
                "already-active direct publish completes before destruction");
    }

    // dropNoticeIsPersistedOnShutdown
    {
        QTemporaryDir overloadDirectory;
        require(overloadDirectory.isValid(), "queue overload temporary directory");
        QString overloadPath;
        quint64 lastPublishedSequence = 0;
        {
            VaporView::LogService service(QStringLiteral("VaporViewOverloadTest"),
                                          &app,
                                          overloadDirectory.path());
            require(waitForText(service.logFilePath(),
                                QByteArrayLiteral("应用日志系统已启动。")),
                    "overload writer lifecycle record is flushed");
            require(waitForCondition([&service]() {
                        return service.writerStateForTest().value(QStringLiteral("size")).toInt() == 0;
                    }),
                    "overload writer is idle before controlled blocking");
            service.setWriterBlockedForTest(true);
            require(service.waitForWriterBlockedForTest(std::chrono::seconds(2)),
                    "overload writer enters controlled blocked state");
            for (int index = 0; index < 10000; ++index)
            {
                lastPublishedSequence = service.publish(
                    VaporView::LogLevel::Debug,
                    QStringLiteral("OverloadTest"),
                    QStringLiteral("queue.overload"),
                    QStringLiteral("overload-record-%1").arg(index)).sequence;
            }
            overloadPath = service.logFilePath();
        }
        bool foundDropNotice = false;
        bool foundShutdownDropNotice = false;
        for (const QJsonObject& object : readJsonLines(overloadPath))
        {
            if (object.value(QStringLiteral("source")).toString() == QStringLiteral("LogService") &&
                object.value(QStringLiteral("category")).toString() == QStringLiteral("queue"))
            {
                foundDropNotice = true;
                require(object.value(QStringLiteral("level")).toString() == QStringLiteral("Warning"),
                        "shutdown drop notice is Warning");
                require(object.value(QStringLiteral("fields")).toObject()
                            .value(QStringLiteral("dropped_count")).toDouble() > 0,
                        "shutdown drop notice reports a positive dropped count");
                if (object.value(QStringLiteral("sequence")).toVariant().toULongLong() >
                    lastPublishedSequence)
                {
                    foundShutdownDropNotice = true;
                }
            }
        }
        require(foundDropNotice, "queue overload produces an aggregated drop notice");
        require(foundShutdownDropNotice,
                "remaining queue drop notice is persisted with a new sequence during shutdown");
    }

    // retentionLimitIncludesNewActiveFile
    {
        QTemporaryDir retentionDirectory;
        require(retentionDirectory.isValid(), "retention temporary directory");
        const QDateTime baseTime = QDateTime::currentDateTimeUtc().addDays(-20);
        QString oldestPath;
        QString newestPath;
        for (int index = 0; index < 10; ++index)
        {
            const QString path = QDir(retentionDirectory.path()).filePath(
                QStringLiteral("VaporViewRetentionTest-2026-06-%1.jsonl")
                    .arg(index + 1, 2, 10, QLatin1Char('0')));
            QFile file(path);
            const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            require(opened, "create pre-existing retention log");
            require(file.write("{\"old\":true}\n") > 0,
                    "write pre-existing retention log");
            require(file.setFileTime(baseTime.addSecs(index), QFileDevice::FileModificationTime),
                    "set deterministic retention modification time");
            file.close();
            if (index == 0) oldestPath = path;
            if (index == 9) newestPath = path;
        }

        QString activePath;
        {
            VaporView::LogService service(QStringLiteral("VaporViewRetentionTest"),
                                          &app,
                                          retentionDirectory.path());
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("Test"),
                            QStringLiteral("retention"),
                            QStringLiteral("retention-active-sentinel"));
            activePath = service.logFilePath();
            require(QFileInfo::exists(activePath), "retention keeps the active file");
        }
        const QFileInfoList retained = QDir(retentionDirectory.path()).entryInfoList(
            {QStringLiteral("VaporViewRetentionTest-*.jsonl"),
             QStringLiteral("VaporViewRetentionTest-*.jsonl.*")},
            QDir::Files,
            QDir::Name);
        require(retained.size() <= 10, "retention limit includes the newly created active file");
        require(!QFileInfo::exists(oldestPath), "retention removes the oldest pre-existing file");
        require(QFileInfo::exists(newestPath), "retention preserves the newest pre-existing file");
        require(QFileInfo::exists(activePath), "retention never deletes the active file");
        qint64 totalBytes = 0;
        for (const QFileInfo& fileInfo : retained)
        {
            totalBytes += fileInfo.size();
        }
        require(totalBytes <= 100LL * 1024LL * 1024LL,
                "retention keeps total bytes within the configured limit");
    }

    // oversizedPublishIsBoundedBeforeQueueAndSignal / moderateOversizeStressIsBounded
    {
        QTemporaryDir stressDirectory;
        require(stressDirectory.isValid(), "oversized stress temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewOversizeStressTest"),
                                      &app,
                                      stressDirectory.path());
        int observedRecords = 0;
        qsizetype largestObservedMessage = 0;
        qsizetype largestObservedLine = 0;
        QObject::connect(&service,
                         &VaporView::LogService::recordPublished,
                         &app,
                         [&](const VaporView::LogRecord& record) {
                             if (record.category != QStringLiteral("oversize.stress"))
                             {
                                 return;
                             }
                             ++observedRecords;
                             largestObservedMessage = (std::max)(largestObservedMessage,
                                                                 record.message.toUtf8().size());
                             largestObservedLine = (std::max)(largestObservedLine,
                                                              record.toJsonLine().size());
                         },
                         Qt::DirectConnection);

        const QString hugeMessage(20 * 1024 * 1024, QLatin1Char('m'));
        QVariantMap hugeFields;
        for (int index = 0; index < 4; ++index)
        {
            hugeFields.insert(QStringLiteral("large-field-%1").arg(index),
                              QString(1024 * 1024, QLatin1Char('v')));
        }

        QElapsedTimer timer;
        timer.start();
        for (int index = 0; index < 3; ++index)
        {
            const VaporView::LogRecord bounded = service.publish(
                VaporView::LogLevel::Error,
                QStringLiteral("OversizeStress"),
                QStringLiteral("oversize.stress"),
                hugeMessage,
                hugeFields);
            require(bounded.message.toUtf8().size() <=
                        VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                    "publish returns the same bounded message stored by the queue");
            require(bounded.toJsonLine().size() <=
                        VaporView::LogRecordLimits::kMaxSerializedRecordBytes,
                    "publish returns a bounded complete record");
        }
        service.publish(VaporView::LogLevel::Critical,
                        QStringLiteral("OversizeStress"),
                        QStringLiteral("oversize.stress.flush"),
                        QStringLiteral("oversize-stress-flush"));
        require(timer.elapsed() < 15000,
                "moderate 20 MiB oversize stress completes within an explicit timeout");
        require(observedRecords == 3,
                "recordPublished observes every bounded oversize record");
        require(largestObservedMessage <= VaporView::LogRecordLimits::kMaxMessageUtf8Bytes,
                "recordPublished never retains the original 20 MiB message");
        require(largestObservedLine <= VaporView::LogRecordLimits::kMaxSerializedRecordBytes,
                "recordPublished payload remains within the JSONL hard limit");
        require(QFileInfo(service.logFilePath()).size() < 2 * 1024 * 1024,
                "moderate oversize stress produces a controlled log file size");
    }

    QTemporaryDir rotationDirectory;
    require(rotationDirectory.isValid(), "temporary rotation directory");
    {
        VaporView::LogService service(QStringLiteral("VaporViewRotationTest"),
                                      &app,
                                      rotationDirectory.path());
        const QString largeMessage(70 * 1024, QLatin1Char('x'));
        for (int index = 0; index < 200; ++index)
        {
            service.publish(VaporView::LogLevel::Debug,
                            QStringLiteral("Test"),
                            QStringLiteral("rotation"),
                            largeMessage);
        }
        require(waitForFile(service.logFilePath() + QStringLiteral(".1"),
                            std::chrono::seconds(15)),
                "JSONL rotates before exceeding 10 MiB");
    }

    const QDir rotationDirectoryView(rotationDirectory.path());
    const QFileInfoList retainedLogs = rotationDirectoryView.entryInfoList(
        {QStringLiteral("VaporViewRotationTest-*.jsonl"),
         QStringLiteral("VaporViewRotationTest-*.jsonl.*")},
        QDir::Files,
        QDir::Time);
    require(retainedLogs.size() <= 10, "rotation retains at most ten files");
    qint64 retainedBytes = 0;
    for (const QFileInfo& fileInfo : retainedLogs)
    {
        retainedBytes += fileInfo.size();
    }
    require(retainedBytes <= 100LL * 1024LL * 1024LL,
            "rotation keeps total log size within the configured limit");

    return 0;
}
