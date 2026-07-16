#pragma once

#include "ground/SessionData.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <limits>

namespace VaporView::Ground
{

struct SessionExportResult
{
    bool success = false;
    QString error;
    qsizetype rowsWritten = 0;
};

class SessionExportService final
{
public:
    static QString trajectoryCsvHeader();
    static SessionExportResult exportTrajectoryCsv(
        const QString& filename,
        const QVector<SessionTrackPoint>& points,
        quint64 startTimestampUs = 0,
        quint64 endTimestampUs = std::numeric_limits<quint64>::max());
};

}  // namespace VaporView::Ground
