#pragma once

#include <QString>
#include <QStringList>
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
    int temperatureSlaveAddress = 1;
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
    using LogCallback = std::function<void(const QString&)>;

    static QStringList availablePorts();
    static SerialPortDetectionOutcome detect(const SerialPortDetectionRequest& request,
                                             CancelCallback cancelRequested,
                                             LogCallback log);
};

}  // namespace VaporView::Ground::Devices
