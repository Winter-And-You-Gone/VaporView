#include "TelemetryLink.h"

namespace VaporView
{

TelemetryLink::TelemetryLink(QObject *parent)
    : QObject(parent)
{
}

QString telemetryTransportName(TelemetryTransportType type)
{
    switch (type)
    {
    case TelemetryTransportType::Tcp:
        return QStringLiteral("tcp");
    case TelemetryTransportType::Serial:
        return QStringLiteral("serial");
    }
    return QStringLiteral("tcp");
}

bool parseTelemetryTransport(const QString& text, TelemetryTransportType& type)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized.isEmpty() ||
        normalized == QStringLiteral("tcp") ||
        normalized == QStringLiteral("network") ||
        normalized == QStringLiteral("net") ||
        normalized == QStringLiteral("ethernet"))
    {
        type = TelemetryTransportType::Tcp;
        return true;
    }
    if (normalized == QStringLiteral("serial") ||
        normalized == QStringLiteral("uart") ||
        normalized == QStringLiteral("com"))
    {
        type = TelemetryTransportType::Serial;
        return true;
    }
    return false;
}

}  // namespace VaporView
