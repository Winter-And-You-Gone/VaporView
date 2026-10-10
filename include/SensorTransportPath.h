#ifndef VaporView_SENSOR_TRANSPORT_PATH_H_
#define VaporView_SENSOR_TRANSPORT_PATH_H_

#include <QString>

namespace VaporView
{

// Selects how sensor data reaches the host.  This is independent of the
// Local/Remote source mode: both source modes may use either sensor path.
enum class SensorTransportPath
{
    DirectDevices,
    FpgaRelay
};

inline QString sensorTransportPathToString(SensorTransportPath path)
{
    return path == SensorTransportPath::FpgaRelay
        ? QStringLiteral("fpga_relay")
        : QStringLiteral("direct_devices");
}

inline bool parseSensorTransportPath(const QString& value, SensorTransportPath& path)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("direct_devices") ||
        normalized == QStringLiteral("direct-devices") ||
        normalized == QStringLiteral("direct"))
    {
        path = SensorTransportPath::DirectDevices;
        return true;
    }
    if (normalized == QStringLiteral("fpga_relay") ||
        normalized == QStringLiteral("fpga-relay") ||
        normalized == QStringLiteral("fpga"))
    {
        path = SensorTransportPath::FpgaRelay;
        return true;
    }
    return false;
}

}  // namespace VaporView

#endif
