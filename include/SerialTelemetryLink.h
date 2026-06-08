#ifndef VaporView_SERIAL_TELEMETRY_LINK_H_
#define VaporView_SERIAL_TELEMETRY_LINK_H_

#include "TelemetryLink.h"

#include <QSerialPort>
#include <QString>

namespace VaporView
{

class SerialTelemetryLink : public TelemetryLink
{
    Q_OBJECT

public:
    explicit SerialTelemetryLink(QObject *parent = nullptr);

    bool open(const QString& portName, int baudRate);
    void close() override;
    bool isOpen() const override;
    QString portName() const;
    QString endpointDescription() const override;
    qint64 writeBytes(const QByteArray& bytes) override;

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    static QString normalizePortName(const QString& portName);

    QSerialPort port_;
    QString requested_port_name_;
};

}  // namespace VaporView

#endif
