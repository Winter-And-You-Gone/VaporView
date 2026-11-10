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
        if (record.fields.value("event") != QStringLiteral("device_collector_output"))
            return {record.message};
        const QString device = record.fields.value("device_id").toString();
        const QString raw = record.fields.value("process_output").toString();
        auto &entry = entries_[device];
        QStringList lines;
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
