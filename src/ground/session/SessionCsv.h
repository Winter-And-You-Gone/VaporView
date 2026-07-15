#pragma once

#include <QString>
#include <QStringList>

namespace VaporView::Ground::SessionCsv
{

QString csvValueAt(const QStringList& fields, int index);
QStringList parseCsvLine(const QString& line);
int findHeaderIndex(const QStringList& headers, const QStringList& candidates);
bool parseBooleanCsvField(const QString& value, bool defaultValue = false);
double parseOptionalDouble(const QString& value);

}  // namespace VaporView::Ground::SessionCsv
