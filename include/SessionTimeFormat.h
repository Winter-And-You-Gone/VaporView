#ifndef VAPORVIEW_SESSION_TIME_FORMAT_H_
#define VAPORVIEW_SESSION_TIME_FORMAT_H_

#include <QString>

namespace VaporView
{

QString formatSessionMetadataTimeBeijing(const QString& utcText);
QString formatSessionDurationText(const QString& startUtc, const QString& endUtc, bool english);

}  // namespace VaporView

#endif  // VAPORVIEW_SESSION_TIME_FORMAT_H_
