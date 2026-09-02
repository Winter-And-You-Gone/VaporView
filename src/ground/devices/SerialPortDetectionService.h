#pragma once

#include "LogRecord.h"
#include "data_collector.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace VaporView::Ground::Devices
{

struct SerialPortProbeSelection
{
    QString port;
    QString baud;
};

struct SerialPortDetectionRequest
{
    bool english = false;
    QStringList availablePorts;
    SerialPortProbeSelection epsilon;
    SerialPortProbeSelection ptb;
    SerialPortProbeSelection hmp;
    SerialPortProbeSelection lidar;
    SerialPortProbeSelection temperatureController;
    SerialPortProbeSelection ai8TemperatureController;
    VaporView::PressureSensorProtocol pressureProtocol = VaporView::PressureSensorProtocol::Ptb210;
    VaporView::HumiditySensorProtocol humidityProtocol = VaporView::HumiditySensorProtocol::Hmp3Modbus;
    int temperatureSlaveAddress = 1;
    int ai8SlaveAddress = 1;
};

struct SerialPortDetectionResult
{
    QString deviceKey;
    QString port;
    QString baud;
};

struct SerialPortDetectionOutcome
{
    QVector<SerialPortDetectionResult> detections;
    bool canceled = false;
};

class SerialPortDetectionService final
{
public:
    using CancelCallback = std::function<bool()>;
    struct LogEntry
    {
        VaporView::LogLevel level = VaporView::LogLevel::Info;
        QString event;
        QString message;
        QVariantMap fields;
    };

    using LogCallback = std::function<void(const LogEntry&)>;

    static QStringList availablePorts();
    static SerialPortDetectionOutcome detect(const SerialPortDetectionRequest& request,
                                             CancelCallback cancelRequested,
                                             LogCallback log);
};

}  // namespace VaporView::Ground::Devices
