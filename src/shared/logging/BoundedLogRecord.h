#ifndef VAPORVIEW_BOUNDED_LOG_RECORD_H_
#define VAPORVIEW_BOUNDED_LOG_RECORD_H_

#include "LogRecord.h"

#include <QByteArray>

namespace VaporView::LoggingInternal
{

LogRecord boundLogRecord(LogRecord record);
QByteArray serializePreparedLogRecord(const LogRecord& record);
QByteArray serializeBoundedLogRecord(const LogRecord& record);

}  // namespace VaporView::LoggingInternal

#endif
