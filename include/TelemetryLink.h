#ifndef VaporView_TELEMETRY_LINK_H_
#define VaporView_TELEMETRY_LINK_H_

#include "LogRecord.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace VaporView
{

enum class TelemetryTransportType
{
    Tcp,
    Serial,
};

class TelemetryLink : public QObject
{
    Q_OBJECT

public:
    explicit TelemetryLink(QObject *parent = nullptr);
    ~TelemetryLink() override = default;

    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString endpointDescription() const = 0;
    virtual qint64 writeBytes(const QByteArray& bytes) = 0;

signals:
    void bytesReceived(const QByteArray& bytes);
    void openChanged(bool open);
    void errorOccurred(const QString& error);
    void logRecordGenerated(const VaporView::LogRecord& record);
};

QString telemetryTransportName(TelemetryTransportType type);
bool parseTelemetryTransport(const QString& text, TelemetryTransportType& type);

}  // namespace VaporView

#endif
