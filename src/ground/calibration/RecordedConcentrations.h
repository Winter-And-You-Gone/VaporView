#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace VaporView::Ground::Calibration
{
struct RecordedConcentrations
{
    bool success = false;
    QString error;
    QJsonObject snapshot;
    QJsonArray rows;
};
// Reads only the parameters embedded in the historical Session/export. Never
// consults the current selection or the source .vcal file.
RecordedConcentrations readRecordedConcentrations(const QString& filename);
bool exportRecordedConcentrations(const QString& filename,const RecordedConcentrations&,QString *error);
}
