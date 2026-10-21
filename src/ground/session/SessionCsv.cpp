#include "ground/session/SessionCsv.h"

#include <limits>

namespace VaporView::Ground::SessionCsv
{

QString csvValueAt(const QStringList& fields, int index)
{
    return index >= 0 && index < fields.size() ? fields.at(index) : QString();
}

QStringList parseCsvLine(const QString& line, QString *error)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;
    bool fieldStarted = false, quoteClosed = false;
    if (error) error->clear();

    for (int index = 0; index < line.size(); ++index)
    {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"'))
        {
            if (inQuotes && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"'))
            {
                current += QLatin1Char('"');
                ++index;
            }
            else
            {
                if (error && !inQuotes && fieldStarted)
                {
                    *error = QStringLiteral("CSV 引号位置无效");
                    return {};
                }
                inQuotes = !inQuotes;
                fieldStarted = true;
                quoteClosed = !inQuotes;
            }
            continue;
        }

        if (character == QLatin1Char(',') && !inQuotes)
        {
            fields.push_back(current);
            current.clear();
            fieldStarted = quoteClosed = false;
        }
        else
        {
            if (error && quoteClosed && !character.isSpace())
            {
                *error = QStringLiteral("CSV 引号后存在多余字符");
                return {};
            }
            current += character;
            fieldStarted = true;
        }
    }

    if (error && inQuotes)
    {
        *error = QStringLiteral("CSV 引号未闭合");
        return {};
    }
    fields.push_back(current);
    return fields;
}

int findHeaderIndex(const QStringList& headers, const QStringList& candidates)
{
    for (const QString& candidate : candidates)
    {
        const int index = headers.indexOf(candidate);
        if (index >= 0)
        {
            return index;
        }
    }
    return -1;
}

bool parseBooleanCsvField(const QString& value, bool defaultValue)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return defaultValue;
    }
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("yes"))
    {
        return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0") ||
        normalized == QStringLiteral("no"))
    {
        return false;
    }
    return defaultValue;
}

double parseOptionalDouble(const QString& value)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
}

}  // namespace VaporView::Ground::SessionCsv
