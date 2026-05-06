#ifndef VaporView_SERIAL_TELEMETRY_LINK_H_
#define VaporView_SERIAL_TELEMETRY_LINK_H_

#include <QObject>
#include <QSerialPort>
#include <QString>

namespace VaporView
{

class SerialTelemetryLink : public QObject
{
    Q_OBJECT

public:
    explicit SerialTelemetryLink(QObject *parent = nullptr);

    bool open(const QString& portName, int baudRate);
    void close();
    bool isOpen() const;
    QString portName() const;
    qint64 writeBytes(const QByteArray& bytes);

signals:
    void bytesReceived(const QByteArray& bytes);
    void openChanged(bool open);
    void errorOccurred(const QString& error);

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
