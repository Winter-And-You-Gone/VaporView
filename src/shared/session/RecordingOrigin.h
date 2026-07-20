#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace VaporView::Session
{

enum class RecordingOrigin
{
    Ground,
    Sky
};

QString recordingOriginToString(RecordingOrigin origin);

std::optional<RecordingOrigin> recordingOriginFromString(QStringView value);

QString recordingOriginDisplayText(RecordingOrigin origin, bool english);

}  // namespace VaporView::Session
