#include "ground/main/UiLogModel.h"

#include <QCoreApplication>
#include <QModelIndex>
#include <QStringList>
#include <QTimeZone>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

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

VaporView::LogRecord makeRecord(VaporView::LogLevel level,
                                const QString& message,
                                QVariantMap fields = {},
                                const QString& source = QStringLiteral("Ground"),
                                const QString& category = QStringLiteral("test"),
                                quint64 sequence = 1,
                                quint64 timestampUs = 1'000'000)
{
    VaporView::LogRecord record;
    record.level = level;
    record.source = source;
    record.category = category;
    record.message = message;
    record.fields = fields;
    record.sequence = sequence;
    record.timestamp_us = timestampUs;
    record.timestamp_utc = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000), QTimeZone::UTC)
        .toString(Qt::ISODateWithMs);
    return record;
}

int proxyRows(VaporView::Ground::Main::UiLogModel& model,
              VaporView::Ground::Main::LogUiViewMode mode,
              const QString& search = {})
{
    VaporView::Ground::Main::UiLogFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.setViewMode(mode);
    proxy.setSearchText(search);
    return proxy.rowCount();
}

QVector<VaporView::LogRecord> queueWithFirstRecord(const VaporView::LogRecord& first,
                                                   VaporView::LogLevel fillLevel,
                                                   QVariantMap fillFields,
                                                   const QString& fillMessage)
{
    QVector<VaporView::LogRecord> records;
    records.reserve(VaporView::Ground::Main::kMaxPendingUiLogRecords);
    records.append(first);
    for (int i = 1; i < VaporView::Ground::Main::kMaxPendingUiLogRecords; ++i)
    {
        records.append(makeRecord(fillLevel,
                                  fillMessage,
                                  fillFields,
                                  QStringLiteral("Ground"),
                                  QStringLiteral("pending"),
                                  static_cast<quint64>(i + 1),
                                  static_cast<quint64>(i + 1) * 1'000'000));
    }
    return records;
}

void defaultViewIsAttention()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Info, QStringLiteral("普通 Info"), {{QStringLiteral("event"), QStringLiteral("ordinary_info")}}));
    model.appendRecord(makeRecord(VaporView::LogLevel::Debug, QStringLiteral("调试"), {{QStringLiteral("event"), QStringLiteral("debug_line")}}, QStringLiteral("Qt"), QStringLiteral("qt"), 2, 2'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("警告"), {{QStringLiteral("event"), QStringLiteral("warning_line")}}, QStringLiteral("Ground"), QStringLiteral("warn"), 3, 3'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Error, QStringLiteral("错误"), {{QStringLiteral("event"), QStringLiteral("error_line")}}, QStringLiteral("Ground"), QStringLiteral("err"), 4, 4'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Critical, QStringLiteral("严重"), {{QStringLiteral("event"), QStringLiteral("critical_line")}}, QStringLiteral("Ground"), QStringLiteral("crit"), 5, 5'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Info, QStringLiteral("重要 Info"), {{QStringLiteral("event"), QStringLiteral("important_info")}, {QStringLiteral("ui_visibility"), QStringLiteral("attention")}}, QStringLiteral("Ground"), QStringLiteral("info"), 6, 6'000'000));

    require(proxyRows(model, LogUiViewMode::Attention) == 4, "default attention view shows warning/error/critical/attention info only");
    require(proxyRows(model, LogUiViewMode::All) == 5, "all view shows info and higher without debug");
    require(proxyRows(model, LogUiViewMode::Debug) == 6, "debug view shows debug and all non-explicit-hidden records");
}

void structuredVisibilityDoesNotUseMessageLanguage()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Info, QStringLiteral("设备连接失败。"), {{QStringLiteral("event"), QStringLiteral("device_connection_status")}}, QStringLiteral("Ground"), QStringLiteral("device"), 1));
    model.appendRecord(makeRecord(VaporView::LogLevel::Info, QStringLiteral("device connection failed"), {{QStringLiteral("event"), QStringLiteral("device_connection_status")}}, QStringLiteral("Ground"), QStringLiteral("device"), 2, 2'000'000));
    require(proxyRows(model, LogUiViewMode::Attention) == 0, "failure words do not promote Info to attention");
    require(proxyRows(model, LogUiViewMode::All) == 2, "legacy info defaults to details");

    model.appendRecord(makeRecord(VaporView::LogLevel::Info, QStringLiteral("隐藏"), {{QStringLiteral("event"), QStringLiteral("hidden_info")}, {QStringLiteral("ui_visibility"), QStringLiteral("hidden")}}, QStringLiteral("Ground"), QStringLiteral("hidden"), 3, 3'000'000));
    require(model.droppedHiddenCount() == 1, "ui_visibility hidden never enters panel model");
    require(proxyRows(model, LogUiViewMode::Debug) == 2, "explicit hidden does not appear in debug view");
}

void detailsLogsDoNotInferSeverityFromMessage()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    const QStringList statusMessages{
        QStringLiteral("失败"),
        QStringLiteral("ERROR"),
        QStringLiteral("设备断开"),
        QStringLiteral("connection failed"),
    };
    quint64 sequence = 1;
    for (const QString& statusMessage : statusMessages)
    {
        model.appendRecord(makeRecord(VaporView::LogLevel::Info,
                                      QStringLiteral("界面状态已更新。"),
                                      {{QStringLiteral("event"), QStringLiteral("ui_status_updated")},
                                       {QStringLiteral("ui_visibility"), QStringLiteral("details")},
                                       {QStringLiteral("ui_message"), statusMessage}},
                                      QStringLiteral("Ground"),
                                      QStringLiteral("ui.status"),
                                      sequence,
                                      sequence * 1'000'000));
        ++sequence;
    }

    require(proxyRows(model, LogUiViewMode::Attention) == 0,
            "detailsLogDoesNotInferLevelFromChineseOrEnglishMessage");
    require(proxyRows(model, LogUiViewMode::All) == statusMessages.size(),
            "detailsInfoLogDefaultsToAllView");
    require(model.index(0, 0).data(UiLogModel::LevelRole).value<VaporView::LogLevel>() ==
                VaporView::LogLevel::Info,
            "details status log remains Info");
}

void searchMatchesStructuredFields()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Warning,
                                  QStringLiteral("设备连接中断"),
                                  {{QStringLiteral("event"), QStringLiteral("device_disconnected")},
                                   {QStringLiteral("error_code"), QStringLiteral("DEVICE_LINK_LOST")},
                                   {QStringLiteral("device_id"), QStringLiteral("epsilon")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("device.connection")));
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("连接中断")) == 1, "search matches Chinese message");
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("device.connection")) == 1, "search matches category");
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("DEVICE_LINK_LOST")) == 1, "search matches error_code");
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("epsilon")) == 1, "search matches field values");
}

void hideSourceCategoryOnlyChangesDisplayText()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Info,
                                  QStringLiteral("设备连接信息"),
                                  {{QStringLiteral("event"), QStringLiteral("device_connection_status")},
                                   {QStringLiteral("ui_visibility"), QStringLiteral("attention")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("device.connection")));

    const QModelIndex index = model.index(0, 0);
    require(index.isValid(), "hide source/category test has one log row");
    require(!model.hideSourceCategory(), "source/category display is visible by default");
    require(index.data(Qt::DisplayRole).toString().contains(QStringLiteral("Ground/device.connection")),
            "default display includes source/category");

    model.setHideSourceCategory(true);
    require(model.hideSourceCategory(), "source/category display toggle is enabled");
    require(!index.data(Qt::DisplayRole).toString().contains(QStringLiteral("Ground/device.connection")),
            "hidden source/category is removed from display text");
    require(index.data(Qt::DisplayRole).toString().contains(QStringLiteral("设备连接信息")),
            "hidden source/category display keeps the log message");
    require(index.data(UiLogModel::SourceRole).toString() == QStringLiteral("Ground") &&
                index.data(UiLogModel::CategoryRole).toString() == QStringLiteral("device.connection"),
            "hidden source/category keeps structured roles");
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("device.connection")) == 1,
            "hidden source/category keeps structured search text");

    model.setHideSourceCategory(false);
    require(index.data(Qt::DisplayRole).toString().contains(QStringLiteral("Ground/device.connection")),
            "source/category display can be restored");
}

void repeatedWarningsAreAggregated()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 1, 1'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 2, 2'000'000));
    require(model.entryCount() == 1, "same warning aggregates inside window");
    require(model.index(0, 0).data(UiLogModel::RepeatCountRole).toInt() == 2, "repeat count is updated");
    require(model.index(0, 0).data(UiLogModel::FirstSequenceRole).toULongLong() == 1, "first sequence retained");
    require(model.index(0, 0).data(UiLogModel::LastSequenceRole).toULongLong() == 2, "last sequence updated");

    model.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("SkyCore"), QStringLiteral("telemetry"), 3, 3'000'000));
    require(model.entryCount() == 2, "different source is not aggregated");
    model.appendRecord(makeRecord(VaporView::LogLevel::Error, QStringLiteral("通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 4, 4'000'000));
    require(model.entryCount() == 3, "severity escalation is not hidden by aggregation");
    model.appendRecord(makeRecord(VaporView::LogLevel::Critical, QStringLiteral("严重通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 5, 5'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Critical, QStringLiteral("严重通信超时"), {{QStringLiteral("event"), QStringLiteral("telemetry_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 6, 6'000'000));
    require(model.entryCount() == 5, "critical is not aggregated");

    UiLogModel windowModel;
    windowModel.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("窗口超时"), {{QStringLiteral("event"), QStringLiteral("window_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 1, 1'000'000));
    windowModel.appendRecord(makeRecord(VaporView::LogLevel::Warning, QStringLiteral("窗口超时"), {{QStringLiteral("event"), QStringLiteral("window_timeout")}}, QStringLiteral("Ground"), QStringLiteral("telemetry"), 2, 10'000'000));
    require(windowModel.entryCount() == 2, "dedupe window expires");
}

void aggregationPromotesVisibility()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    model.appendRecord(makeRecord(VaporView::LogLevel::Info,
                                  QStringLiteral("设备状态已更新。"),
                                  {{QStringLiteral("event"), QStringLiteral("device_status_updated")},
                                   {QStringLiteral("ui_visibility"), QStringLiteral("details")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("device"),
                                  1,
                                  1'000'000));
    model.appendRecord(makeRecord(VaporView::LogLevel::Info,
                                  QStringLiteral("设备状态已更新。"),
                                  {{QStringLiteral("event"), QStringLiteral("device_status_updated")},
                                   {QStringLiteral("ui_visibility"), QStringLiteral("attention")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("device"),
                                  2,
                                  2'000'000));
    require(model.entryCount() == 1, "same Info event remains aggregated when visibility changes");
    require(model.index(0, 0).data(UiLogModel::VisibilityRole).toString() == QStringLiteral("attention"),
            "merged record promotes details to attention visibility");
    require(model.index(0, 0).data(UiLogModel::ExplicitVisibilityRole).toBool(),
            "merged record keeps explicit visibility when later record supplies it");
    require(proxyRows(model, LogUiViewMode::Attention) == 1,
            "attention view shows a merged record that was promoted to attention");

    model.appendRecord(makeRecord(VaporView::LogLevel::Info,
                                  QStringLiteral("设备状态已更新。"),
                                  {{QStringLiteral("event"), QStringLiteral("device_status_updated")},
                                   {QStringLiteral("ui_visibility"), QStringLiteral("details")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("device"),
                                  3,
                                  3'000'000));
    require(model.entryCount() == 1, "later details record still aggregates");
    require(model.index(0, 0).data(UiLogModel::VisibilityRole).toString() == QStringLiteral("attention"),
            "merged record never downgrades attention visibility");
    require(proxyRows(model, LogUiViewMode::Attention) == 1,
            "attention view keeps showing an entry after a later details duplicate");
}

void pendingAttentionInfoDoesNotEvictWarning()
{
    using namespace VaporView::Ground::Main;
    const auto warning = makeRecord(VaporView::LogLevel::Warning,
                                    QStringLiteral("需要保留的警告"),
                                    {{QStringLiteral("event"), QStringLiteral("pending_warning_retained")}},
                                    QStringLiteral("Ground"),
                                    QStringLiteral("pending"),
                                    1,
                                    1'000'000);
    const auto incomingAttentionInfo = makeRecord(
        VaporView::LogLevel::Info,
        QStringLiteral("关注状态"),
        {{QStringLiteral("event"), QStringLiteral("pending_attention_info")},
         {QStringLiteral("ui_visibility"), QStringLiteral("attention")}},
        QStringLiteral("Ground"),
        QStringLiteral("pending"),
        10'000,
        10'000'000'000);
    const QVector<VaporView::LogRecord> records = queueWithFirstRecord(
        warning,
        VaporView::LogLevel::Info,
        {{QStringLiteral("event"), QStringLiteral("pending_attention_info")},
         {QStringLiteral("ui_visibility"), QStringLiteral("attention")}},
        QStringLiteral("关注状态"));

    const int dropRow = uiLogPendingDropRow(records, incomingAttentionInfo);
    require(dropRow > 0, "pending attention Info does not evict an existing Warning");
    require(records.at(dropRow).level == VaporView::LogLevel::Info,
            "pending attention Info evicts another attention Info before a Warning");
}

void pendingErrorDoesNotEvictCritical()
{
    using namespace VaporView::Ground::Main;
    const auto critical = makeRecord(VaporView::LogLevel::Critical,
                                     QStringLiteral("需要保留的严重错误"),
                                     {{QStringLiteral("event"), QStringLiteral("pending_critical_retained")}},
                                     QStringLiteral("Ground"),
                                     QStringLiteral("pending"),
                                     1,
                                     1'000'000);
    const auto incomingError = makeRecord(VaporView::LogLevel::Error,
                                          QStringLiteral("错误"),
                                          {{QStringLiteral("event"), QStringLiteral("pending_error")}},
                                          QStringLiteral("Ground"),
                                          QStringLiteral("pending"),
                                          10'000,
                                          10'000'000'000);
    const QVector<VaporView::LogRecord> records = queueWithFirstRecord(
        critical,
        VaporView::LogLevel::Error,
        {{QStringLiteral("event"), QStringLiteral("pending_error")}},
        QStringLiteral("错误"));

    const int dropRow = uiLogPendingDropRow(records, incomingError);
    require(dropRow > 0, "pending Error does not evict an existing Critical");
    require(records.at(dropRow).level == VaporView::LogLevel::Error,
            "pending Error evicts another Error before a Critical");
}

void pendingCriticalEvictsLowerSeverityFirst()
{
    using namespace VaporView::Ground::Main;
    QVector<VaporView::LogRecord> records;
    records.reserve(kMaxPendingUiLogRecords);
    records.append(makeRecord(VaporView::LogLevel::Critical,
                              QStringLiteral("最早的严重错误"),
                              {{QStringLiteral("event"), QStringLiteral("pending_critical_oldest")}},
                              QStringLiteral("Ground"),
                              QStringLiteral("pending"),
                              1,
                              1'000'000));
    records.append(makeRecord(VaporView::LogLevel::Error,
                              QStringLiteral("可淘汰的错误"),
                              {{QStringLiteral("event"), QStringLiteral("pending_error_evictable")}},
                              QStringLiteral("Ground"),
                              QStringLiteral("pending"),
                              2,
                              2'000'000));
    for (int i = 2; i < kMaxPendingUiLogRecords; ++i)
    {
        records.append(makeRecord(VaporView::LogLevel::Critical,
                                  QStringLiteral("严重错误 %1").arg(i),
                                  {{QStringLiteral("event"), QStringLiteral("pending_critical_%1").arg(i)}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("pending"),
                                  static_cast<quint64>(i + 1),
                                  static_cast<quint64>(i + 1) * 1'000'000));
    }
    const auto incomingCritical = makeRecord(VaporView::LogLevel::Critical,
                                             QStringLiteral("新严重错误"),
                                             {{QStringLiteral("event"), QStringLiteral("pending_critical_incoming")}},
                                             QStringLiteral("Ground"),
                                             QStringLiteral("pending"),
                                             20'000,
                                             20'000'000'000);

    const int dropRow = uiLogPendingDropRow(records, incomingCritical);
    require(dropRow == 1, "pending Critical evicts lower-severity Error before an older Critical");
}

void capacityAndBatchingAreBounded()
{
    using namespace VaporView::Ground::Main;
    UiLogModel model;
    QVector<VaporView::LogRecord> burst;
    for (int i = 0; i < kMaxUiLogEntries + 100; ++i)
    {
        burst.append(makeRecord(VaporView::LogLevel::Info,
                                QStringLiteral("info %1").arg(i),
                                {{QStringLiteral("event"), QStringLiteral("info_burst_%1").arg(i)}},
                                QStringLiteral("Ground"),
                                QStringLiteral("burst"),
                                static_cast<quint64>(i + 1),
                                static_cast<quint64>(i + 1) * 1'000'000));
    }
    model.appendRecords(burst);
    require(model.entryCount() == kMaxUiLogEntries, "ui log model has hard limit");
    require(model.insertBatchCountForTest() == 1, "burst inserted as one model batch");
    model.appendRecord(makeRecord(VaporView::LogLevel::Critical,
                                  QStringLiteral("关键错误"),
                                  {{QStringLiteral("event"), QStringLiteral("recent_critical")}},
                                  QStringLiteral("Ground"),
                                  QStringLiteral("critical"),
                                  99'999,
                                  99'999'000'000));
    require(model.entryCount() == kMaxUiLogEntries, "critical insert preserves total hard limit");
    require(proxyRows(model, LogUiViewMode::Attention, QStringLiteral("recent_critical")) == 1,
            "info burst does not evict recent critical prematurely");
    require(kMaxPendingUiLogRecords == 1000, "pending UI queue has a centralized hard limit");
    model.clearEntries();
    require(model.entryCount() == 0, "clear releases UI entries");
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    defaultViewIsAttention();
    structuredVisibilityDoesNotUseMessageLanguage();
    detailsLogsDoNotInferSeverityFromMessage();
    searchMatchesStructuredFields();
    hideSourceCategoryOnlyChangesDisplayText();
    repeatedWarningsAreAggregated();
    aggregationPromotesVisibility();
    pendingAttentionInfoDoesNotEvictWarning();
    pendingErrorDoesNotEvictCritical();
    pendingCriticalEvictsLowerSeverityFirst();
    capacityAndBatchingAreBounded();
    return 0;
}
