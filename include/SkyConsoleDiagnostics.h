#pragma once

#include "LogService.h"
#include <QHash>
#include <QStringList>

namespace VaporView {

// Console-only aggregation: every original record still reaches LogService.
class SkyConsoleDiagnostics
{
public:
    QStringList accept(const LogRecord& record, qint64 nowMs)
    {
        const auto event = record.fields.value("event").toString();
        if (event == QStringLiteral("fpga_worker_started")) return {};
        QString key;
        QString raw;
        QStringList lines;
        if (event == QStringLiteral("fpga_operation_finished")) {
            key = QStringLiteral("fpga/%1/%2/%3").arg(record.fields.value("fpga_op").toString(),
                    record.fields.value("fpga_source").toString(), record.fields.value("channel").toString());
            if (record.level != LogLevel::Info) {
                auto entry = entries_.find(key);
                if (entry != entries_.end()) {
                    appendRepeated(*entry, lines);
                    entries_.erase(entry);
                }
                lines.append(record.message);
                return lines;
            }
        } else if (event == QStringLiteral("device_collector_output")) {
            key = QStringLiteral("collector/") + record.fields.value("device_id").toString();
            raw = record.fields.value("process_output").toString();
        } else {
            return {record.message};
        }
        auto &entry = entries_[key];
        if (entry.message != record.message || entry.raw != raw) {
            appendRepeated(entry, lines);
            entry = {record.message, raw, nowMs, 0};
            lines.append(record.message);
        } else {
            ++entry.repeated;
            if (nowMs - entry.lastOutputMs >= 5000) {
                appendRepeated(entry, lines);
                entry.lastOutputMs = nowMs;
            }
        }
        return lines;
    }

    QStringList flush(qint64 nowMs, bool force = false)
    {
        QStringList lines;
        for (auto &entry : entries_) {
            if (force || nowMs - entry.lastOutputMs >= 5000) {
                appendRepeated(entry, lines);
                entry.lastOutputMs = nowMs;
            }
        }
        return lines;
    }

private:
    struct Entry { QString message; QString raw; qint64 lastOutputMs = 0; quint64 repeated = 0; };
    static void appendRepeated(Entry &entry, QStringList &lines)
    {
        if (!entry.repeated) return;
        lines.append(QStringLiteral("%1 [重复 %2 次]").arg(entry.message).arg(entry.repeated));
        entry.repeated = 0;
    }
    QHash<QString, Entry> entries_;
};
}
