#include "logging/BoundedLogRecord.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QSet>
#include <QTime>
#include <QUrl>
#include <QVariantHash>
#include <QVariantList>

#include <algorithm>
#include <limits>
#include <utility>

namespace VaporView::LoggingInternal
{

namespace
{
constexpr qsizetype kSequenceSerializationHeadroomBytes = 32;
constexpr qsizetype kMetadataReserveBytes = 2048;
constexpr qsizetype kMinimumMetadataBudgetBytes = 2048;
constexpr qsizetype kMinimumMessageBytes = 32;
constexpr char kTruncationMarker[] = "...<truncated>";

struct TruncatedString
{
    QString value;
    qint64 original_utf8_bytes = 0;
    qint64 original_json_bytes = 2;
    bool truncated = false;
};

struct TruncationState
{
    QStringList reasons;
    qint64 original_message_utf8_bytes = 0;
    qint64 original_fields_json_bytes = 2;
    bool original_fields_size_is_lower_bound = false;
    qint64 structurally_dropped_fields = 0;
    qint64 size_dropped_fields = 0;
    qint64 dropped_container_elements = 0;
    qint64 truncated_strings = 0;
    qint64 truncated_byte_arrays = 0;
    qint64 unsupported_values = 0;
    qint64 reserved_field_collisions = 0;
    qsizetype visited_nodes = 0;
    bool node_limit_marker_emitted = false;

    void addReason(const QString& reason)
    {
        if (!reasons.contains(reason))
        {
            reasons.push_back(reason);
        }
    }

    bool isTruncated() const
    {
        return !reasons.isEmpty();
    }
};

struct SanitizedValue
{
    QJsonValue value;
    qsizetype json_bytes = 0;
    qint64 original_json_bytes = 0;
    bool original_size_is_exact = true;
    bool accepted = false;
};

qint64 saturatedAdd(qint64 left, qint64 right)
{
    if (right > 0 && left > (std::numeric_limits<qint64>::max)() - right)
    {
        return (std::numeric_limits<qint64>::max)();
    }
    return left + right;
}

qint64 jsonStringBytesFromUtf8(const QByteArray& utf8)
{
    qint64 bytes = 2;
    for (const char character : utf8)
    {
        const uchar value = static_cast<uchar>(character);
        switch (value)
        {
        case '"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            bytes = saturatedAdd(bytes, 2);
            break;
        default:
            bytes = saturatedAdd(bytes, value < 0x20 ? 6 : 1);
            break;
        }
    }
    return bytes;
}

TruncatedString truncateUtf8(const QString& value, qsizetype maxBytes)
{
    TruncatedString result;
    const QByteArray utf8 = value.toUtf8();
    result.original_utf8_bytes = utf8.size();
    result.original_json_bytes = jsonStringBytesFromUtf8(utf8);
    if (utf8.size() <= maxBytes)
    {
        result.value = value;
        return result;
    }

    result.truncated = true;
    const QByteArray marker(kTruncationMarker);
    if (maxBytes <= 0)
    {
        return result;
    }
    if (maxBytes <= marker.size())
    {
        result.value = QString::fromLatin1(marker.constData(), maxBytes);
        return result;
    }

    qsizetype prefixBytes = maxBytes - marker.size();
    while (prefixBytes > 0 && prefixBytes < utf8.size() &&
           (static_cast<uchar>(utf8.at(prefixBytes)) & 0xC0U) == 0x80U)
    {
        --prefixBytes;
    }
    result.value = QString::fromUtf8(utf8.constData(), prefixBytes) +
        QString::fromLatin1(marker);
    return result;
}

qsizetype compactJsonSize(const QJsonValue& value)
{
    if (value.isObject())
    {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).size();
    }
    if (value.isArray())
    {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).size();
    }
    QJsonArray wrapper;
    wrapper.append(value);
    return QJsonDocument(wrapper).toJson(QJsonDocument::Compact).size() - 2;
}

qsizetype compactJsonStringSize(const QString& value)
{
    return compactJsonSize(QJsonValue(value));
}

QByteArray serializeRawRecord(const LogRecord& record)
{
    QByteArray line = QJsonDocument(record.toJsonObject()).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

QString variantTypeName(const QVariant& value)
{
    const char *name = value.metaType().name();
    return name && *name ? QString::fromLatin1(name) : QStringLiteral("unknown");
}

SanitizedValue makeScalarResult(const QJsonValue& value,
                                qint64 originalBytes,
                                bool originalSizeIsExact,
                                qsizetype maxBytes)
{
    const qsizetype bytes = compactJsonSize(value);
    if (bytes > maxBytes)
    {
        return {};
    }
    return {value, bytes, originalBytes, originalSizeIsExact, true};
}

SanitizedValue makeTypeDescriptor(const QString& key,
                                  const QString& typeName,
                                  qint64 originalBytes,
                                  qsizetype maxBytes)
{
    QJsonObject descriptor;
    descriptor.insert(key, typeName);
    return makeScalarResult(descriptor, originalBytes, false, maxBytes);
}

bool isReservedLogField(const QString& key)
{
    return key.startsWith(QStringLiteral("_log_"));
}

SanitizedValue sanitizeVariant(const QVariant& value,
                               int depth,
                               qsizetype maxBytes,
                               TruncationState& state);

SanitizedValue sanitizeStringValue(const QString& value,
                                   qsizetype maxBytes,
                                   TruncationState& state,
                                   const QString& reason)
{
    const TruncatedString bounded = truncateUtf8(
        value, LogRecordLimits::kMaxSingleStringUtf8Bytes);
    if (bounded.truncated)
    {
        ++state.truncated_strings;
        state.addReason(reason);
    }
    return makeScalarResult(QJsonValue(bounded.value),
                            bounded.original_json_bytes,
                            true,
                            maxBytes);
}

QList<std::pair<QString, QVariant>> stableHashEntries(const QVariantHash& hash)
{
    QStringList retainedKeys;
    retainedKeys.reserve((std::min)(hash.size(), LogRecordLimits::kMaxContainerElements));
    for (auto iterator = hash.cbegin(); iterator != hash.cend(); ++iterator)
    {
        const auto position = std::lower_bound(retainedKeys.cbegin(),
                                               retainedKeys.cend(),
                                               iterator.key());
        if (retainedKeys.size() < LogRecordLimits::kMaxContainerElements)
        {
            retainedKeys.insert(position, iterator.key());
        }
        else if (position != retainedKeys.cend())
        {
            retainedKeys.insert(position, iterator.key());
            retainedKeys.removeLast();
        }
    }

    QList<std::pair<QString, QVariant>> entries;
    entries.reserve(retainedKeys.size());
    for (const QString& key : std::as_const(retainedKeys))
    {
        entries.push_back({key, hash.value(key)});
    }
    return entries;
}

SanitizedValue sanitizeMapEntries(const QList<std::pair<QString, QVariant>>& entries,
                                  qsizetype originalElementCount,
                                  int depth,
                                  qsizetype maxBytes,
                                  TruncationState& state,
                                  bool topLevelFields)
{
    if (maxBytes < 2)
    {
        return {};
    }
    if (depth >= LogRecordLimits::kMaxVariantDepth)
    {
        state.addReason(QStringLiteral("variant_depth_limit"));
        state.dropped_container_elements = saturatedAdd(state.dropped_container_elements,
                                                        originalElementCount);
        return makeTypeDescriptor(QStringLiteral("_log_depth_limited_type"),
                                  QStringLiteral("map"),
                                  2,
                                  maxBytes);
    }

    QJsonObject object;
    qsizetype bytes = 2;
    qint64 originalBytes = 2;
    bool originalSizeIsExact = entries.size() == originalElementCount;
    qsizetype processed = 0;
    for (const auto& entry : entries)
    {
        ++processed;
        if (processed > LogRecordLimits::kMaxContainerElements)
        {
            break;
        }
        if (topLevelFields && isReservedLogField(entry.first))
        {
            ++state.structurally_dropped_fields;
            ++state.reserved_field_collisions;
            state.addReason(QStringLiteral("reserved_field_collision"));
            originalSizeIsExact = false;
            continue;
        }

        const TruncatedString boundedKey = truncateUtf8(
            entry.first, LogRecordLimits::kMaxSingleStringUtf8Bytes);
        if (boundedKey.truncated)
        {
            ++state.truncated_strings;
            state.addReason(QStringLiteral("field_key_limit"));
        }
        if (object.contains(boundedKey.value))
        {
            if (topLevelFields)
            {
                ++state.structurally_dropped_fields;
            }
            else
            {
                ++state.dropped_container_elements;
            }
            state.addReason(QStringLiteral("field_key_collision"));
            originalSizeIsExact = false;
            continue;
        }

        const qsizetype keyBytes = compactJsonStringSize(boundedKey.value);
        const qsizetype separatorBytes = object.isEmpty() ? 0 : 1;
        const qsizetype fixedBytes = separatorBytes + keyBytes + 1;
        if (bytes + fixedBytes >= maxBytes)
        {
            originalSizeIsExact = false;
            break;
        }
        SanitizedValue child = sanitizeVariant(entry.second,
                                                depth + 1,
                                                maxBytes - bytes - fixedBytes,
                                                state);
        if (!child.accepted)
        {
            originalSizeIsExact = false;
            break;
        }
        object.insert(boundedKey.value, child.value);
        bytes += fixedBytes + child.json_bytes;
        originalBytes = saturatedAdd(originalBytes,
                                     (object.size() > 1 ? 1 : 0) +
                                         boundedKey.original_json_bytes + 1 +
                                         child.original_json_bytes);
        originalSizeIsExact = originalSizeIsExact && child.original_size_is_exact;
    }

    const qsizetype dropped = (std::max)(qsizetype(0),
                                         originalElementCount - object.size());
    if (dropped > 0)
    {
        if (topLevelFields)
        {
            const qint64 alreadyCounted = state.structurally_dropped_fields;
            state.structurally_dropped_fields =
                (std::max)(alreadyCounted, static_cast<qint64>(dropped));
        }
        else
        {
            state.dropped_container_elements = saturatedAdd(state.dropped_container_elements,
                                                            dropped);
        }
        state.addReason(originalElementCount > LogRecordLimits::kMaxContainerElements
                            ? QStringLiteral("container_element_limit")
                            : QStringLiteral("fields_size_limit"));
        originalSizeIsExact = false;
        originalBytes = saturatedAdd(originalBytes, 1);
    }
    return {object, bytes, originalBytes, originalSizeIsExact, true};
}

SanitizedValue sanitizeList(const QVariantList& list,
                            int depth,
                            qsizetype maxBytes,
                            TruncationState& state)
{
    if (maxBytes < 2)
    {
        return {};
    }
    if (depth >= LogRecordLimits::kMaxVariantDepth)
    {
        state.addReason(QStringLiteral("variant_depth_limit"));
        state.dropped_container_elements = saturatedAdd(state.dropped_container_elements,
                                                        list.size());
        return makeTypeDescriptor(QStringLiteral("_log_depth_limited_type"),
                                  QStringLiteral("list"),
                                  2,
                                  maxBytes);
    }

    QJsonArray array;
    qsizetype bytes = 2;
    qint64 originalBytes = 2;
    bool originalSizeIsExact = true;
    const qsizetype count = (std::min)(list.size(), LogRecordLimits::kMaxContainerElements);
    for (qsizetype index = 0; index < count; ++index)
    {
        const qsizetype separatorBytes = array.isEmpty() ? 0 : 1;
        SanitizedValue child = sanitizeVariant(list.at(index),
                                                depth + 1,
                                                maxBytes - bytes - separatorBytes,
                                                state);
        if (!child.accepted)
        {
            originalSizeIsExact = false;
            break;
        }
        array.append(child.value);
        bytes += separatorBytes + child.json_bytes;
        originalBytes = saturatedAdd(originalBytes,
                                     separatorBytes + child.original_json_bytes);
        originalSizeIsExact = originalSizeIsExact && child.original_size_is_exact;
    }
    const qsizetype dropped = list.size() - array.size();
    if (dropped > 0)
    {
        state.dropped_container_elements = saturatedAdd(state.dropped_container_elements,
                                                        dropped);
        state.addReason(list.size() > LogRecordLimits::kMaxContainerElements
                            ? QStringLiteral("container_element_limit")
                            : QStringLiteral("fields_size_limit"));
        originalSizeIsExact = false;
        originalBytes = saturatedAdd(originalBytes, 1);
    }
    return {array, bytes, originalBytes, originalSizeIsExact, true};
}

SanitizedValue sanitizeVariant(const QVariant& value,
                               int depth,
                               qsizetype maxBytes,
                               TruncationState& state)
{
    ++state.visited_nodes;
    if (state.visited_nodes > LogRecordLimits::kMaxVariantNodes)
    {
        state.addReason(QStringLiteral("variant_node_limit"));
        if (state.node_limit_marker_emitted)
        {
            return {};
        }
        state.node_limit_marker_emitted = true;
        return makeTypeDescriptor(QStringLiteral("_log_limited_type"),
                                  variantTypeName(value),
                                  1,
                                  maxBytes);
    }
    if (!value.isValid() || value.isNull())
    {
        return makeScalarResult(QJsonValue(QJsonValue::Null), 4, true, maxBytes);
    }

    switch (value.metaType().id())
    {
    case QMetaType::QString:
        return sanitizeStringValue(value.toString(),
                                   maxBytes,
                                   state,
                                   QStringLiteral("field_value_limit"));
    case QMetaType::QByteArray:
    {
        const QByteArray bytes = value.toByteArray();
        const bool truncated = bytes.size() > LogRecordLimits::kMaxByteArrayBytes;
        const QByteArray bounded = truncated
            ? QByteArray(bytes.constData(), LogRecordLimits::kMaxByteArrayBytes)
            : bytes;
        if (truncated)
        {
            ++state.truncated_byte_arrays;
            state.addReason(QStringLiteral("byte_array_limit"));
        }
        QJsonValue jsonValue = QJsonValue::fromVariant(QVariant(bounded));
        if (jsonValue.isUndefined())
        {
            jsonValue = QString::fromUtf8(bounded);
        }
        const qint64 originalBytes = truncated
            ? saturatedAdd(bytes.size(), 2)
            : compactJsonSize(jsonValue);
        return makeScalarResult(jsonValue, originalBytes, !truncated, maxBytes);
    }
    case QMetaType::QVariantMap:
    {
        const QVariantMap map = value.toMap();
        QList<std::pair<QString, QVariant>> entries;
        entries.reserve((std::min)(map.size(), LogRecordLimits::kMaxContainerElements));
        qsizetype count = 0;
        for (auto iterator = map.cbegin(); iterator != map.cend() &&
             count < LogRecordLimits::kMaxContainerElements; ++iterator, ++count)
        {
            entries.push_back({iterator.key(), iterator.value()});
        }
        return sanitizeMapEntries(entries, map.size(), depth, maxBytes, state, false);
    }
    case QMetaType::QVariantHash:
    {
        const QVariantHash hash = value.toHash();
        return sanitizeMapEntries(stableHashEntries(hash),
                                  hash.size(),
                                  depth,
                                  maxBytes,
                                  state,
                                  false);
    }
    case QMetaType::QVariantList:
        return sanitizeList(value.toList(), depth, maxBytes, state);
    case QMetaType::QStringList:
    {
        QVariantList list;
        const QStringList strings = value.toStringList();
        list.reserve((std::min)(strings.size(), LogRecordLimits::kMaxContainerElements));
        const qsizetype count = (std::min)(strings.size(),
                                          LogRecordLimits::kMaxContainerElements);
        for (qsizetype index = 0; index < count; ++index)
        {
            list.push_back(strings.at(index));
        }
        if (strings.size() > count)
        {
            state.dropped_container_elements = saturatedAdd(state.dropped_container_elements,
                                                            strings.size() - count);
            state.addReason(QStringLiteral("container_element_limit"));
            state.original_fields_size_is_lower_bound = true;
        }
        return sanitizeList(list, depth, maxBytes, state);
    }
    case QMetaType::QJsonObject:
        return sanitizeVariant(value.toJsonObject().toVariantMap(), depth, maxBytes, state);
    case QMetaType::QJsonArray:
        return sanitizeVariant(value.toJsonArray().toVariantList(), depth, maxBytes, state);
    case QMetaType::QJsonDocument:
    {
        const QJsonDocument document = value.value<QJsonDocument>();
        return document.isObject()
            ? sanitizeVariant(document.object().toVariantMap(), depth, maxBytes, state)
            : sanitizeVariant(document.array().toVariantList(), depth, maxBytes, state);
    }
    case QMetaType::QJsonValue:
        return sanitizeVariant(value.value<QJsonValue>().toVariant(), depth, maxBytes, state);
    case QMetaType::QDate:
        return sanitizeStringValue(value.toDate().toString(Qt::ISODate),
                                   maxBytes,
                                   state,
                                   QStringLiteral("field_value_limit"));
    case QMetaType::QTime:
        return sanitizeStringValue(value.toTime().toString(Qt::ISODateWithMs),
                                   maxBytes,
                                   state,
                                   QStringLiteral("field_value_limit"));
    case QMetaType::QDateTime:
        return sanitizeStringValue(value.toDateTime().toString(Qt::ISODateWithMs),
                                   maxBytes,
                                   state,
                                   QStringLiteral("field_value_limit"));
    case QMetaType::QUrl:
        return sanitizeStringValue(value.toUrl().toString(),
                                   maxBytes,
                                   state,
                                   QStringLiteral("field_value_limit"));
    default:
        break;
    }

    const QJsonValue jsonValue = QJsonValue::fromVariant(value);
    if (value.metaType().id() < QMetaType::User && !jsonValue.isNull() &&
        !jsonValue.isUndefined() && !jsonValue.isObject() && !jsonValue.isArray())
    {
        return makeScalarResult(jsonValue,
                                compactJsonSize(jsonValue),
                                true,
                                maxBytes);
    }
    ++state.unsupported_values;
    state.addReason(QStringLiteral("unsupported_variant_type"));
    return makeTypeDescriptor(QStringLiteral("_unsupported_type"),
                              variantTypeName(value),
                              1,
                              maxBytes);
}

QJsonObject truncationMetadata(const TruncationState& state)
{
    QJsonObject metadata;
    if (!state.isTruncated())
    {
        return metadata;
    }
    QStringList reasons = state.reasons;
    std::sort(reasons.begin(), reasons.end());
    metadata.insert(QStringLiteral("_log_truncated"), true);
    metadata.insert(QStringLiteral("_log_truncation_reasons"),
                    QJsonArray::fromStringList(reasons));
    if (state.original_message_utf8_bytes > 0)
    {
        metadata.insert(QStringLiteral("_log_original_message_utf8_bytes"),
                        state.original_message_utf8_bytes);
    }
    metadata.insert(QStringLiteral("_log_original_fields_json_bytes"),
                    state.original_fields_json_bytes);
    if (state.original_fields_size_is_lower_bound)
    {
        metadata.insert(QStringLiteral("_log_original_fields_size_is_lower_bound"), true);
    }
    const qint64 droppedFields = saturatedAdd(state.structurally_dropped_fields,
                                              state.size_dropped_fields);
    if (droppedFields > 0)
    {
        metadata.insert(QStringLiteral("_log_dropped_field_count"), droppedFields);
    }
    if (state.dropped_container_elements > 0)
    {
        metadata.insert(QStringLiteral("_log_dropped_container_elements"),
                        state.dropped_container_elements);
    }
    if (state.truncated_strings > 0)
    {
        metadata.insert(QStringLiteral("_log_truncated_string_count"),
                        state.truncated_strings);
    }
    if (state.truncated_byte_arrays > 0)
    {
        metadata.insert(QStringLiteral("_log_truncated_byte_array_count"),
                        state.truncated_byte_arrays);
    }
    if (state.unsupported_values > 0)
    {
        metadata.insert(QStringLiteral("_log_unsupported_value_count"),
                        state.unsupported_values);
    }
    if (state.reserved_field_collisions > 0)
    {
        metadata.insert(QStringLiteral("_log_reserved_field_collision_count"),
                        state.reserved_field_collisions);
    }
    return metadata;
}

QStringList sortedKeys(const QJsonObject& object)
{
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

qsizetype appendObjectValue(QJsonObject& target,
                            qsizetype currentBytes,
                            const QString& key,
                            const QJsonValue& value)
{
    const qsizetype separatorBytes = target.isEmpty() ? 0 : 1;
    return currentBytes + separatorBytes + compactJsonStringSize(key) + 1 +
        compactJsonSize(value);
}

QJsonObject composeFields(const QJsonObject& businessFields,
                          TruncationState& state,
                          qsizetype maxBytes,
                          const QString& sizeReason)
{
    maxBytes = (std::max)(maxBytes, kMinimumMetadataBudgetBytes);
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const QJsonObject metadata = truncationMetadata(state);
        QJsonObject result;
        qsizetype bytes = 2;
        for (const QString& key : sortedKeys(metadata))
        {
            const QJsonValue value = metadata.value(key);
            const qsizetype nextBytes = appendObjectValue(result, bytes, key, value);
            if (nextBytes > maxBytes)
            {
                break;
            }
            result.insert(key, value);
            bytes = nextBytes;
        }

        qsizetype retained = 0;
        for (const QString& key : sortedKeys(businessFields))
        {
            const QJsonValue value = businessFields.value(key);
            const qsizetype nextBytes = appendObjectValue(result, bytes, key, value);
            if (nextBytes > maxBytes)
            {
                break;
            }
            result.insert(key, value);
            bytes = nextBytes;
            ++retained;
        }
        const qint64 dropped = businessFields.size() - retained;
        if (dropped == state.size_dropped_fields)
        {
            return result;
        }
        state.size_dropped_fields = dropped;
        if (dropped > 0)
        {
            state.addReason(sizeReason);
        }
    }

    QJsonObject fallback = truncationMetadata(state);
    while (QJsonDocument(fallback).toJson(QJsonDocument::Compact).size() > maxBytes &&
           fallback.size() > 2)
    {
        fallback.remove(sortedKeys(fallback).constLast());
    }
    return fallback;
}

void shrinkString(QString& value, qsizetype maxBytes)
{
    value = truncateUtf8(value, maxBytes).value;
}

void shrinkMessageToFit(LogRecord& record,
                        TruncationState& state,
                        qsizetype targetBytes)
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const QByteArray line = serializeRawRecord(record);
        if (line.size() <= targetBytes)
        {
            return;
        }
        const qsizetype currentBytes = record.message.toUtf8().size();
        if (currentBytes <= kMinimumMessageBytes)
        {
            return;
        }
        const qsizetype excess = line.size() - targetBytes;
        const qsizetype nextBytes = (std::max)(
            kMinimumMessageBytes,
            currentBytes > excess + 128 ? currentBytes - excess - 128 : kMinimumMessageBytes);
        shrinkString(record.message, nextBytes);
        state.addReason(QStringLiteral("record_size_limit"));
    }
}

LogRecord minimalBoundedRecord(LogRecord record, const TruncationState& state)
{
    shrinkString(record.timestamp_utc, 128);
    shrinkString(record.source, 1024);
    shrinkString(record.category, 1024);
    shrinkString(record.message, 4096);
    record.correlation_id.clear();
    record.session_id.clear();
    QJsonObject metadata = truncationMetadata(state);
    metadata.insert(QStringLiteral("_log_truncated"), true);
    metadata.insert(QStringLiteral("_log_truncation_reasons"),
                    QJsonArray{QStringLiteral("record_size_limit")});
    record.fields = metadata.toVariantMap();
    return record;
}

}  // namespace

LogRecord boundLogRecord(LogRecord record)
{
    TruncationState state;
    const TruncatedString message = truncateUtf8(record.message,
                                                 LogRecordLimits::kMaxMessageUtf8Bytes);
    qint64 boundedTopLevelUtf8Bytes = (std::min)(
        message.original_utf8_bytes,
        static_cast<qint64>(LogRecordLimits::kMaxMessageUtf8Bytes));
    state.original_message_utf8_bytes = message.original_utf8_bytes;
    record.message = message.value;
    if (message.truncated)
    {
        state.addReason(QStringLiteral("message_limit"));
    }

    auto boundTopLevelString = [&state, &boundedTopLevelUtf8Bytes](QString& value) {
        const TruncatedString bounded = truncateUtf8(
            value, LogRecordLimits::kMaxSingleStringUtf8Bytes);
        value = bounded.value;
        boundedTopLevelUtf8Bytes = saturatedAdd(
            boundedTopLevelUtf8Bytes,
            (std::min)(bounded.original_utf8_bytes,
                       static_cast<qint64>(LogRecordLimits::kMaxSingleStringUtf8Bytes)));
        if (bounded.truncated)
        {
            ++state.truncated_strings;
            state.addReason(QStringLiteral("top_level_string_limit"));
        }
    };
    boundTopLevelString(record.timestamp_utc);
    boundTopLevelString(record.source);
    boundTopLevelString(record.category);
    boundTopLevelString(record.correlation_id);
    boundTopLevelString(record.session_id);

    QList<std::pair<QString, QVariant>> entries;
    entries.reserve((std::min)(record.fields.size(), LogRecordLimits::kMaxContainerElements));
    qsizetype entryCount = 0;
    for (auto iterator = record.fields.cbegin(); iterator != record.fields.cend() &&
         entryCount < LogRecordLimits::kMaxContainerElements; ++iterator, ++entryCount)
    {
        entries.push_back({iterator.key(), iterator.value()});
    }
    SanitizedValue fields = sanitizeMapEntries(entries,
                                               record.fields.size(),
                                               0,
                                               LogRecordLimits::kMaxFieldsJsonBytes,
                                               state,
                                               true);
    QJsonObject businessFields = fields.accepted ? fields.value.toObject() : QJsonObject();
    state.original_fields_json_bytes = fields.accepted ? fields.original_json_bytes : 2;
    state.original_fields_size_is_lower_bound =
        state.original_fields_size_is_lower_bound ||
        (fields.accepted && !fields.original_size_is_exact);

    QJsonObject finalFields = composeFields(businessFields,
                                            state,
                                            LogRecordLimits::kMaxFieldsJsonBytes,
                                            QStringLiteral("fields_size_limit"));
    record.fields = finalFields.toVariantMap();

    const qsizetype targetBytes = LogRecordLimits::kMaxSerializedRecordBytes -
        kSequenceSerializationHeadroomBytes;
    const qint64 conservativeBytes = saturatedAdd(
        1024 + compactJsonSize(finalFields),
        boundedTopLevelUtf8Bytes > (std::numeric_limits<qint64>::max)() / 6
            ? (std::numeric_limits<qint64>::max)()
            : boundedTopLevelUtf8Bytes * 6);
    if (conservativeBytes <= targetBytes)
    {
        return record;
    }
    QByteArray line = serializeRawRecord(record);
    if (line.size() <= targetBytes)
    {
        return record;
    }

    state.addReason(QStringLiteral("record_size_limit"));
    LogRecord emptyFieldsRecord = record;
    emptyFieldsRecord.fields.clear();
    const qsizetype envelopeBytes = serializeRawRecord(emptyFieldsRecord).size();
    const qsizetype allowedFieldsBytes = envelopeBytes < targetBytes
        ? (std::max)(kMinimumMetadataBudgetBytes,
                     targetBytes - envelopeBytes + qsizetype(2) - kMetadataReserveBytes)
        : kMinimumMetadataBudgetBytes;
    finalFields = composeFields(businessFields,
                                state,
                                (std::min)(allowedFieldsBytes,
                                           LogRecordLimits::kMaxFieldsJsonBytes),
                                QStringLiteral("record_size_limit"));
    record.fields = finalFields.toVariantMap();
    shrinkMessageToFit(record, state, targetBytes);
    line = serializeRawRecord(record);

    if (line.size() > targetBytes)
    {
        record.correlation_id.clear();
        record.session_id.clear();
    }
    if (serializeRawRecord(record).size() > targetBytes)
    {
        shrinkString(record.source, 4096);
        shrinkString(record.category, 4096);
        shrinkMessageToFit(record, state, targetBytes);
    }
    if (serializeRawRecord(record).size() > targetBytes)
    {
        record = minimalBoundedRecord(std::move(record), state);
    }
    return record;
}

QByteArray serializePreparedLogRecord(const LogRecord& record)
{
    // Writer and emergency paths pass records through boundLogRecord() before
    // queueing. Keep the hot path to one JSON serialization and only rebalance
    // if a future caller violates that invariant.
    QByteArray line = serializeRawRecord(record);
    if (line.size() <= LogRecordLimits::kMaxSerializedRecordBytes)
    {
        return line;
    }
    const LogRecord bounded = boundLogRecord(record);
    line = serializeRawRecord(bounded);
    if (line.size() <= LogRecordLimits::kMaxSerializedRecordBytes)
    {
        return line;
    }
    return serializeRawRecord(minimalBoundedRecord(bounded, TruncationState{}));
}

QByteArray serializeBoundedLogRecord(const LogRecord& record)
{
    return serializePreparedLogRecord(boundLogRecord(record));
}

}  // namespace VaporView::LoggingInternal
