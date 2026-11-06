#include "LogService.h"

VaporView::LogRecord publishFromOtherFile(VaporView::LogService& service, int& expectedLine)
{
    expectedLine = __LINE__ + 1;
    return service.publish(VaporView::LogLevel::Info, QStringLiteral("Test"), QStringLiteral("location"), QStringLiteral("other"));
}
