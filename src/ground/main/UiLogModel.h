#pragma once

#include "LogRecord.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QVector>

namespace VaporView::Ground::Main
{

inline constexpr int kMaxUiLogEntries = 2000;
inline constexpr int kMaxAttentionUiLogEntries = 500;
inline constexpr int kMaxPendingUiLogRecords = 1000;
inline constexpr int kUiLogDedupeWindowMs = 5000;
inline constexpr int kUiLogBatchIntervalMs = 50;

enum class LogUiViewMode
{
    Attention,
    All,
    Debug,
};

enum class LogUiVisibility
{
    Attention,
    Details,
    Hidden,
};

struct UiLogVisibilityDecision
{
    LogUiVisibility visibility = LogUiVisibility::Details;
    bool explicitVisibility = false;
};

struct UiLogEntry
{
    VaporView::LogRecord record;
    QDateTime firstTimestamp;
    QDateTime lastTimestamp;
    int repeatCount = 1;
    quint64 firstSequence = 0;
    quint64 lastSequence = 0;
    QString dedupeKey;
    LogUiVisibility visibility = LogUiVisibility::Details;
    bool explicitVisibility = false;
    bool unread = false;
};

UiLogVisibilityDecision uiLogVisibilityForRecord(const VaporView::LogRecord& record);
bool uiLogRecordVisibleInMode(const VaporView::LogRecord& record, LogUiViewMode mode);
QString uiLogDisplayMessage(const VaporView::LogRecord& record);
QString uiLogEvent(const VaporView::LogRecord& record);
QString uiLogErrorCode(const VaporView::LogRecord& record);
QString uiLogDedupeKey(const VaporView::LogRecord& record);
int uiLogPendingRetentionPriority(const VaporView::LogRecord& record);
int uiLogPendingDropRow(const QVector<VaporView::LogRecord>& records,
                        const VaporView::LogRecord& incoming);
QString uiLogViewModeToSetting(LogUiViewMode mode);
LogUiViewMode uiLogViewModeFromSetting(const QString& value);

class UiLogModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        TimestampRole = Qt::UserRole + 1,
        LastTimestampRole,
        LevelRole,
        LevelNameRole,
        SourceRole,
        CategoryRole,
        EventRole,
        ErrorCodeRole,
        MessageRole,
        FieldsRole,
        RepeatCountRole,
        FirstSequenceRole,
        LastSequenceRole,
        VisibilityRole,
        ExplicitVisibilityRole,
        UnreadRole,
        SearchTextRole,
    };
    Q_ENUM(Role)

    explicit UiLogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool appendRecord(const VaporView::LogRecord& record);
    int appendRecords(const QVector<VaporView::LogRecord>& records);
    void clearEntries();
    void clearUnread();
    void markRowsRead(const QModelIndexList& sourceRows);

    const UiLogEntry *entryAt(int row) const;
    int entryCount() const;
    int attentionEntryCount() const;
    int droppedHiddenCount() const;
    int insertBatchCountForTest() const;

private:
    static QDateTime recordTimestamp(const VaporView::LogRecord& record);
    static QString formatEntryDisplay(const UiLogEntry& entry);
    static QString searchableText(const UiLogEntry& entry);
    static bool shouldSkipPanelRecord(const UiLogVisibilityDecision& decision);

    UiLogEntry makeEntry(const VaporView::LogRecord& record) const;
    bool canAggregate(const UiLogEntry& entry, const VaporView::LogRecord& record) const;
    void aggregateInto(UiLogEntry& entry, const VaporView::LogRecord& record);
    void enforceCapacity();
    int firstRemovableRowForTotalLimit() const;
    int firstRemovableAttentionRow() const;

    QVector<UiLogEntry> entries_;
    int dropped_hidden_count_ = 0;
    int insert_batch_count_for_test_ = 0;
};

class UiLogFilterProxyModel final : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit UiLogFilterProxyModel(QObject *parent = nullptr);

    void setViewMode(LogUiViewMode mode);
    LogUiViewMode viewMode() const;
    void setSearchText(const QString& text);
    QString searchText() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    LogUiViewMode view_mode_ = LogUiViewMode::Attention;
    QString search_text_;
};

class UiLogItemDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit UiLogItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}  // namespace VaporView::Ground::Main
