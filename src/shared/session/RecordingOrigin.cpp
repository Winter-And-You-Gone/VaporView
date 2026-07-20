#include "shared/session/RecordingOrigin.h"

namespace VaporView::Session
{

QString recordingOriginToString(RecordingOrigin origin)
{
    switch (origin)
    {
    case RecordingOrigin::Ground:
        return QStringLiteral("ground");
    case RecordingOrigin::Sky:
        return QStringLiteral("sky");
    }
    return QStringLiteral("ground");
}

std::optional<RecordingOrigin> recordingOriginFromString(QStringView value)
{
    const QString normalized = value.trimmed().toString().toLower();
    if (normalized == QLatin1String("ground"))
    {
        return RecordingOrigin::Ground;
    }
    if (normalized == QLatin1String("sky"))
    {
        return RecordingOrigin::Sky;
    }
    return std::nullopt;
}

QString recordingOriginDisplayText(RecordingOrigin origin, bool english)
{
    switch (origin)
    {
    case RecordingOrigin::Ground:
        return english ? QStringLiteral("Ground") : QStringLiteral("地面端");
    case RecordingOrigin::Sky:
        return english ? QStringLiteral("Sky") : QStringLiteral("天空端");
    }
    return english ? QStringLiteral("Ground") : QStringLiteral("地面端");
}

}  // namespace VaporView::Session
