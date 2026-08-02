#include "ground/main/UiLogModel.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStyle>

#include <algorithm>
#include <limits>

namespace VaporView::Ground::Main
{
namespace
{

QString visibilityName(LogUiVisibility visibility)
{
    switch (visibility)
    {
    case LogUiVisibility::Attention:
        return QStringLiteral("attention");
    case LogUiVisibility::Details:
        return QStringLiteral("details");
    case LogUiVisibility::Hidden:
        return QStringLiteral("hidden");
    }
    return QStringLiteral("details");
}

int visibilityRank(LogUiVisibility visibility)
{
    switch (visibility)
    {
    case LogUiVisibility::Hidden:
        return 0;
    case LogUiVisibility::Details:
        return 1;
    case LogUiVisibility::Attention:
        return 2;
    }
    return 1;
}

QString levelText(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return QStringLiteral("调试");
    case LogLevel::Info:
        return QStringLiteral("信息");
    case LogLevel::Warning:
        return QStringLiteral("警告");
    case LogLevel::Error:
        return QStringLiteral("错误");
    case LogLevel::Critical:
        return QStringLiteral("严重");
    }
    return QStringLiteral("信息");
}

QColor levelColor(LogLevel level, const QPalette& palette)
{
    switch (level)
    {
    case LogLevel::Debug:
        return palette.color(QPalette::Disabled, QPalette::Text);
    case LogLevel::Info:
        return palette.color(QPalette::Text);
    case LogLevel::Warning:
        return QColor(176, 111, 0);
    case LogLevel::Error:
        return QColor(192, 0, 0);
    case LogLevel::Critical:
        return QColor(140, 0, 0);
    }
    return palette.color(QPalette::Text);
}

QString fieldsSearchText(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QVariantMap)
    {
        QStringList parts;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
        {
            parts << it.key();
            parts << fieldsSearchText(it.value());
        }
        return parts.join(QLatin1Char(' '));
    }
    if (value.metaType().id() == QMetaType::QVariantList)
    {
        QStringList parts;
        const QVariantList list = value.toList();
        for (const QVariant& item : list)
        {
            parts << fieldsSearchText(item);
        }
        return parts.join(QLatin1Char(' '));
    }
    return value.toString();
}

bool fieldBool(const QVariantMap& fields, const QString& key, bool defaultValue)
{
    if (!fields.contains(key))
    {
        return defaultValue;
    }
    return fields.value(key).toBool();
}

}  // namespace

UiLogVisibilityDecision uiLogVisibilityForRecord(const VaporView::LogRecord& record)
{
    const QVariant raw = record.fields.value(QStringLiteral("ui_visibility"));
    if (raw.isValid())
    {
        const QString value = raw.toString().trimmed().toLower();
        if (value == QStringLiteral("attention"))
        {
            return {LogUiVisibility::Attention, true};
        }
        if (value == QStringLiteral("details"))
        {
            return {LogUiVisibility::Details, true};
        }
        if (value == QStringLiteral("hidden"))
        {
            return {LogUiVisibility::Hidden, true};
        }
    }

    if (record.level >= VaporView::LogLevel::Warning)
    {
        return {LogUiVisibility::Attention, false};
    }
    if (record.level == VaporView::LogLevel::Debug)
    {
        return {LogUiVisibility::Hidden, false};
    }
    return {LogUiVisibility::Details, false};
}

bool uiLogRecordVisibleInMode(const VaporView::LogRecord& record, LogUiViewMode mode)
{
    const UiLogVisibilityDecision decision = uiLogVisibilityForRecord(record);
    if (decision.explicitVisibility && decision.visibility == LogUiVisibility::Hidden)
    {
        return false;
    }

    switch (mode)
    {
    case LogUiViewMode::Attention:
        return decision.visibility == LogUiVisibility::Attention;
    case LogUiViewMode::All:
        return record.level != VaporView::LogLevel::Debug &&
            decision.visibility != LogUiVisibility::Hidden;
    case LogUiViewMode::Debug:
        return true;
    }
    return false;
}

QString uiLogDisplayMessage(const VaporView::LogRecord& record)
{
    const QString uiMessage = record.fields.value(QStringLiteral("ui_message")).toString();
    return uiMessage.isEmpty() ? record.message : uiMessage;
}

QString uiLogEvent(const VaporView::LogRecord& record)
{
    return record.fields.value(QStringLiteral("event")).toString();
}

QString uiLogErrorCode(const VaporView::LogRecord& record)
{
    const QString errorCode = record.fields.value(QStringLiteral("error_code")).toString();
    if (!errorCode.isEmpty())
    {
        return errorCode;
    }
    return record.fields.value(QStringLiteral("reason_code")).toString();
}

QString uiLogDedupeKey(const VaporView::LogRecord& record)
{
    const QString explicitKey = record.fields.value(QStringLiteral("ui_dedupe_key")).toString();
    const QString event = uiLogEvent(record);
    const QString code = uiLogErrorCode(record);
    const QString message = uiLogDisplayMessage(record);
    QStringList parts{
        record.source,
        record.category,
        QString::number(static_cast<int>(record.level)),
    };
    if (!event.isEmpty())
    {
        parts << event;
        if (!explicitKey.isEmpty())
        {
            parts << explicitKey;
        }
        else
        {
            parts << message << code;
        }
    }
    else
    {
        parts << message << code;
    }
    return parts.join(QChar(0x1f));
}

int uiLogPendingRetentionPriority(const VaporView::LogRecord& record)
{
    const auto decision = uiLogVisibilityForRecord(record);
    if (record.level == VaporView::LogLevel::Debug ||
        (decision.explicitVisibility && decision.visibility == LogUiVisibility::Hidden))
    {
        return 0;
    }
    if (record.level == VaporView::LogLevel::Critical)
    {
        return 5;
    }
    if (record.level == VaporView::LogLevel::Error)
    {
        return 4;
    }
    if (record.level == VaporView::LogLevel::Warning)
    {
        return 3;
    }
    if (record.level == VaporView::LogLevel::Info &&
        decision.visibility == LogUiVisibility::Attention)
    {
        return 2;
    }
    return 1;
}

int uiLogPendingDropRow(const QVector<VaporView::LogRecord>& records,
                        const VaporView::LogRecord& incoming)
{
    int lowestPriority = std::numeric_limits<int>::max();
    int row = -1;
    for (int i = 0; i < records.size(); ++i)
    {
        const int priority = uiLogPendingRetentionPriority(records.at(i));
        if (priority < lowestPriority)
        {
            lowestPriority = priority;
            row = i;
        }
    }

    if (row < 0 || uiLogPendingRetentionPriority(incoming) < lowestPriority)
    {
        return -1;
    }
    return row;
}

QString uiLogViewModeToSetting(LogUiViewMode mode)
{
    switch (mode)
    {
    case LogUiViewMode::Attention:
        return QStringLiteral("attention");
    case LogUiViewMode::All:
        return QStringLiteral("all");
    case LogUiViewMode::Debug:
        return QStringLiteral("debug");
    }
    return QStringLiteral("attention");
}

LogUiViewMode uiLogViewModeFromSetting(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("all"))
    {
        return LogUiViewMode::All;
    }
    if (normalized == QStringLiteral("debug"))
    {
        return LogUiViewMode::Debug;
    }
    return LogUiViewMode::Attention;
}

UiLogModel::UiLogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int UiLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : entries_.size();
}

QVariant UiLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
    {
        return {};
    }

    const UiLogEntry& entry = entries_.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return formatEntryDisplay(entry);
    case TimestampRole:
        return entry.firstTimestamp;
    case LastTimestampRole:
        return entry.lastTimestamp;
    case LevelRole:
        return static_cast<int>(entry.record.level);
    case LevelNameRole:
        return VaporView::logLevelName(entry.record.level);
    case SourceRole:
        return entry.record.source;
    case CategoryRole:
        return entry.record.category;
    case EventRole:
        return uiLogEvent(entry.record);
    case ErrorCodeRole:
        return uiLogErrorCode(entry.record);
    case MessageRole:
        return uiLogDisplayMessage(entry.record);
    case FieldsRole:
        return entry.record.fields;
    case RepeatCountRole:
        return entry.repeatCount;
    case FirstSequenceRole:
        return QVariant::fromValue(entry.firstSequence);
    case LastSequenceRole:
        return QVariant::fromValue(entry.lastSequence);
    case VisibilityRole:
        return visibilityName(entry.visibility);
    case ExplicitVisibilityRole:
        return entry.explicitVisibility;
    case UnreadRole:
        return entry.unread;
    case SearchTextRole:
        return searchableText(entry);
    default:
        return {};
    }
}

QHash<int, QByteArray> UiLogModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names.insert(TimestampRole, "timestamp");
    names.insert(LastTimestampRole, "lastTimestamp");
    names.insert(LevelRole, "level");
    names.insert(LevelNameRole, "levelName");
    names.insert(SourceRole, "source");
    names.insert(CategoryRole, "category");
    names.insert(EventRole, "event");
    names.insert(ErrorCodeRole, "errorCode");
    names.insert(MessageRole, "message");
    names.insert(FieldsRole, "fields");
    names.insert(RepeatCountRole, "repeatCount");
    names.insert(FirstSequenceRole, "firstSequence");
    names.insert(LastSequenceRole, "lastSequence");
    names.insert(VisibilityRole, "visibility");
    names.insert(ExplicitVisibilityRole, "explicitVisibility");
    names.insert(UnreadRole, "unread");
    names.insert(SearchTextRole, "searchText");
    return names;
}

bool UiLogModel::appendRecord(const VaporView::LogRecord& record)
{
    return appendRecords(QVector<VaporView::LogRecord>{record}) > 0;
}

int UiLogModel::appendRecords(const QVector<VaporView::LogRecord>& records)
{
    if (records.isEmpty())
    {
        return 0;
    }

    QVector<UiLogEntry> pendingEntries;
    int accepted = 0;
    int changedExistingRow = -1;

    for (const VaporView::LogRecord& record : records)
    {
        const UiLogVisibilityDecision decision = uiLogVisibilityForRecord(record);
        if (shouldSkipPanelRecord(decision))
        {
            ++dropped_hidden_count_;
            continue;
        }

        if (!pendingEntries.isEmpty() && canAggregate(pendingEntries.last(), record))
        {
            aggregateInto(pendingEntries.last(), record);
            ++accepted;
            continue;
        }
        if (pendingEntries.isEmpty() && !entries_.isEmpty() && canAggregate(entries_.last(), record))
        {
            aggregateInto(entries_.last(), record);
            changedExistingRow = entries_.size() - 1;
            ++accepted;
            continue;
        }

        pendingEntries.push_back(makeEntry(record));
        ++accepted;
    }

    if (changedExistingRow >= 0)
    {
        const QModelIndex changed = index(changedExistingRow, 0);
        emit dataChanged(changed, changed);
    }

    if (!pendingEntries.isEmpty())
    {
        const int first = entries_.size();
        const int last = first + pendingEntries.size() - 1;
        beginInsertRows(QModelIndex(), first, last);
        entries_ += pendingEntries;
        ++insert_batch_count_for_test_;
        endInsertRows();
    }

    enforceCapacity();
    return accepted;
}

void UiLogModel::clearEntries()
{
    if (entries_.isEmpty())
    {
        return;
    }
    beginRemoveRows(QModelIndex(), 0, entries_.size() - 1);
    entries_.clear();
    endRemoveRows();
}

void UiLogModel::clearUnread()
{
    bool changed = false;
    for (UiLogEntry& entry : entries_)
    {
        if (entry.unread)
        {
            entry.unread = false;
            changed = true;
        }
    }
    if (changed && !entries_.isEmpty())
    {
        emit dataChanged(index(0, 0), index(entries_.size() - 1, 0), {UnreadRole});
    }
}

void UiLogModel::markRowsRead(const QModelIndexList& sourceRows)
{
    for (const QModelIndex& modelIndex : sourceRows)
    {
        if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= entries_.size())
        {
            continue;
        }
        UiLogEntry& entry = entries_[modelIndex.row()];
        if (!entry.unread)
        {
            continue;
        }
        entry.unread = false;
        emit dataChanged(modelIndex, modelIndex, {UnreadRole});
    }
}

const UiLogEntry *UiLogModel::entryAt(int row) const
{
    if (row < 0 || row >= entries_.size())
    {
        return nullptr;
    }
    return &entries_.at(row);
}

int UiLogModel::entryCount() const
{
    return entries_.size();
}

int UiLogModel::attentionEntryCount() const
{
    int count = 0;
    for (const UiLogEntry& entry : entries_)
    {
        if (entry.visibility == LogUiVisibility::Attention)
        {
            ++count;
        }
    }
    return count;
}

int UiLogModel::droppedHiddenCount() const
{
    return dropped_hidden_count_;
}

int UiLogModel::insertBatchCountForTest() const
{
    return insert_batch_count_for_test_;
}

QDateTime UiLogModel::recordTimestamp(const VaporView::LogRecord& record)
{
    QDateTime timestamp = QDateTime::fromString(record.timestamp_utc, Qt::ISODateWithMs);
    if (!timestamp.isValid() && record.timestamp_us > 0)
    {
        timestamp = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(record.timestamp_us / 1000), Qt::UTC);
    }
    if (!timestamp.isValid())
    {
        timestamp = QDateTime::currentDateTimeUtc();
    }
    return timestamp;
}

QString UiLogModel::formatEntryDisplay(const UiLogEntry& entry)
{
    const QString first = entry.firstTimestamp.toLocalTime().toString(QStringLiteral("hh:mm:ss"));
    const QString last = entry.lastTimestamp.toLocalTime().toString(QStringLiteral("hh:mm:ss"));
    const QString timeText = entry.repeatCount > 1 && first != last
        ? QStringLiteral("%1-%2").arg(first, last)
        : first;
    const QString repeatText = entry.repeatCount > 1
        ? QStringLiteral("  x%1").arg(entry.repeatCount)
        : QString();
    const QString context = entry.record.source.isEmpty()
        ? entry.record.category
        : (entry.record.category.isEmpty()
            ? entry.record.source
            : QStringLiteral("%1/%2").arg(entry.record.source, entry.record.category));
    return QStringLiteral("%1  %2  %3  %4%5")
        .arg(timeText,
             levelText(entry.record.level),
             context,
             uiLogDisplayMessage(entry.record),
             repeatText);
}

QString UiLogModel::searchableText(const UiLogEntry& entry)
{
    QStringList parts{
        VaporView::logLevelName(entry.record.level),
        entry.record.source,
        entry.record.category,
        uiLogEvent(entry.record),
        uiLogErrorCode(entry.record),
        entry.record.message,
        uiLogDisplayMessage(entry.record),
        fieldsSearchText(entry.record.fields),
    };
    return parts.join(QLatin1Char(' '));
}

bool UiLogModel::shouldSkipPanelRecord(const UiLogVisibilityDecision& decision)
{
    return decision.explicitVisibility && decision.visibility == LogUiVisibility::Hidden;
}

UiLogEntry UiLogModel::makeEntry(const VaporView::LogRecord& record) const
{
    const UiLogVisibilityDecision decision = uiLogVisibilityForRecord(record);
    UiLogEntry entry;
    entry.record = record;
    entry.firstTimestamp = recordTimestamp(record);
    entry.lastTimestamp = entry.firstTimestamp;
    entry.firstSequence = record.sequence;
    entry.lastSequence = record.sequence;
    entry.dedupeKey = uiLogDedupeKey(record);
    entry.visibility = decision.visibility;
    entry.explicitVisibility = decision.explicitVisibility;
    entry.unread = true;
    return entry;
}

bool UiLogModel::canAggregate(const UiLogEntry& entry, const VaporView::LogRecord& record) const
{
    if (entry.record.level == VaporView::LogLevel::Critical ||
        record.level == VaporView::LogLevel::Critical ||
        !fieldBool(record.fields, QStringLiteral("ui_dedupe"), true) ||
        !fieldBool(entry.record.fields, QStringLiteral("ui_dedupe"), true))
    {
        return false;
    }
    if (entry.dedupeKey != uiLogDedupeKey(record))
    {
        return false;
    }

    const QDateTime timestamp = recordTimestamp(record);
    return entry.lastTimestamp.msecsTo(timestamp) >= 0 &&
        entry.lastTimestamp.msecsTo(timestamp) <= kUiLogDedupeWindowMs;
}

void UiLogModel::aggregateInto(UiLogEntry& entry, const VaporView::LogRecord& record)
{
    const UiLogVisibilityDecision decision = uiLogVisibilityForRecord(record);
    entry.record = record;
    entry.lastTimestamp = recordTimestamp(record);
    entry.lastSequence = record.sequence;
    entry.repeatCount += 1;
    if (visibilityRank(decision.visibility) > visibilityRank(entry.visibility))
    {
        entry.visibility = decision.visibility;
    }
    entry.explicitVisibility = entry.explicitVisibility || decision.explicitVisibility;
    entry.unread = true;
}

void UiLogModel::enforceCapacity()
{
    while (attentionEntryCount() > kMaxAttentionUiLogEntries)
    {
        const int row = firstRemovableAttentionRow();
        if (row < 0)
        {
            break;
        }
        beginRemoveRows(QModelIndex(), row, row);
        entries_.removeAt(row);
        endRemoveRows();
    }

    while (entries_.size() > kMaxUiLogEntries)
    {
        const int row = firstRemovableRowForTotalLimit();
        if (row < 0)
        {
            break;
        }
        beginRemoveRows(QModelIndex(), row, row);
        entries_.removeAt(row);
        endRemoveRows();
    }
}

int UiLogModel::firstRemovableRowForTotalLimit() const
{
    for (int i = 0; i < entries_.size(); ++i)
    {
        if (entries_.at(i).visibility != LogUiVisibility::Attention)
        {
            return i;
        }
    }
    return entries_.isEmpty() ? -1 : 0;
}

int UiLogModel::firstRemovableAttentionRow() const
{
    for (int i = 0; i < entries_.size(); ++i)
    {
        if (entries_.at(i).visibility == LogUiVisibility::Attention)
        {
            return i;
        }
    }
    return -1;
}

UiLogFilterProxyModel::UiLogFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void UiLogFilterProxyModel::setViewMode(LogUiViewMode mode)
{
    if (view_mode_ == mode)
    {
        return;
    }
    view_mode_ = mode;
    invalidateFilter();
}

LogUiViewMode UiLogFilterProxyModel::viewMode() const
{
    return view_mode_;
}

void UiLogFilterProxyModel::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed();
    if (search_text_ == normalized)
    {
        return;
    }
    search_text_ = normalized;
    invalidateFilter();
}

QString UiLogFilterProxyModel::searchText() const
{
    return search_text_;
}

bool UiLogFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!sourceIndex.isValid())
    {
        return false;
    }

    const QString visibility = sourceIndex.data(UiLogModel::VisibilityRole).toString();
    const bool explicitVisibility = sourceIndex.data(UiLogModel::ExplicitVisibilityRole).toBool();
    const int level = sourceIndex.data(UiLogModel::LevelRole).toInt();
    bool visible = false;
    if (explicitVisibility && visibility == QStringLiteral("hidden"))
    {
        visible = false;
    }
    else if (view_mode_ == LogUiViewMode::Attention)
    {
        visible = visibility == QStringLiteral("attention");
    }
    else if (view_mode_ == LogUiViewMode::All)
    {
        visible = level != static_cast<int>(VaporView::LogLevel::Debug) &&
            visibility != QStringLiteral("hidden");
    }
    else
    {
        visible = true;
    }

    if (!visible)
    {
        return false;
    }
    if (search_text_.isEmpty())
    {
        return true;
    }
    return sourceIndex.data(UiLogModel::SearchTextRole)
        .toString()
        .contains(search_text_, Qt::CaseInsensitive);
}

UiLogItemDelegate::UiLogItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void UiLogItemDelegate::paint(QPainter *painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

    painter->save();
    const QRect textRect = opt.rect.adjusted(10, 3, -10, -3);
    const auto level = static_cast<VaporView::LogLevel>(index.data(UiLogModel::LevelRole).toInt());
    QFont font = opt.font;
    if (level >= VaporView::LogLevel::Warning)
    {
        font.setBold(true);
    }
    painter->setFont(font);
    painter->setPen(levelColor(level, opt.palette));
    painter->drawText(textRect,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());
    painter->restore();
}

QSize UiLogItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), 26));
    return size;
}

}  // namespace VaporView::Ground::Main
