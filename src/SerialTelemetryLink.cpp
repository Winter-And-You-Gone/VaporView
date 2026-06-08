#include "SerialTelemetryLink.h"

#include <QRegularExpression>

namespace VaporView
{

SerialTelemetryLink::SerialTelemetryLink(QObject *parent)
    : TelemetryLink(parent)
{
    connect(&port_, &QSerialPort::readyRead, this, &SerialTelemetryLink::onReadyRead);
    connect(&port_, &QSerialPort::errorOccurred, this, &SerialTelemetryLink::onErrorOccurred);
}

bool SerialTelemetryLink::open(const QString& portName, int baudRate)
{
    close();
    requested_port_name_ = portName.trimmed();
    port_.setPortName(normalizePortName(requested_port_name_));
    port_.setBaudRate(baudRate);
    port_.setDataBits(QSerialPort::Data8);
    port_.setParity(QSerialPort::NoParity);
    port_.setStopBits(QSerialPort::OneStop);
    port_.setFlowControl(QSerialPort::NoFlowControl);
    if (!port_.open(QIODevice::ReadWrite))
    {
        emit errorOccurred(port_.errorString());
        return false;
    }
    port_.clear();
    emit openChanged(true);
    return true;
}

void SerialTelemetryLink::close()
{
    if (port_.isOpen())
    {
        port_.close();
        emit openChanged(false);
    }
}

bool SerialTelemetryLink::isOpen() const
{
    return port_.isOpen();
}

QString SerialTelemetryLink::portName() const
{
    return requested_port_name_;
}

QString SerialTelemetryLink::endpointDescription() const
{
    return QStringLiteral("serial://%1:%2:8:n:1:off")
        .arg(requested_port_name_)
        .arg(port_.baudRate());
}

qint64 SerialTelemetryLink::writeBytes(const QByteArray& bytes)
{
    if (!port_.isOpen())
    {
        return -1;
    }
    return port_.write(bytes);
}

void SerialTelemetryLink::onReadyRead()
{
    const QByteArray bytes = port_.readAll();
    if (!bytes.isEmpty())
    {
        emit bytesReceived(bytes);
    }
}

void SerialTelemetryLink::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
    {
        return;
    }
    emit errorOccurred(port_.errorString());
    if (error == QSerialPort::ResourceError)
    {
        close();
    }
}

QString SerialTelemetryLink::normalizePortName(const QString& portName)
{
#ifdef _WIN32
    static const QRegularExpression comHighPort(QStringLiteral("^COM(\\d{2,})$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = comHighPort.match(portName.trimmed());
    if (match.hasMatch() && !portName.startsWith(QStringLiteral("\\\\.\\")))
    {
        return QStringLiteral("\\\\.\\") + portName.trimmed();
    }
#endif
    return portName.trimmed();
}

}  // namespace VaporView
