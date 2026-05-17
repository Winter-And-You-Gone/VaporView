#ifndef VaporView_SKY_CONFIG_H_
#define VaporView_SKY_CONFIG_H_

#include <QJsonObject>
#include <QString>

namespace VaporView
{

struct SerialDeviceConfig
{
    bool enabled = true;
    QString port;
    int baud_rate = 115200;
    double frequency_hz = 10.0;

    bool operator==(const SerialDeviceConfig& other) const;
    bool operator!=(const SerialDeviceConfig& other) const;
};

struct WaveTcpConfig
{
    bool enabled = true;
    QString host = QStringLiteral("127.0.0.1");
    int port = 8888;
    int downsample_ratio = 10;
    int peak_search_start_index = 0;
    int peak_search_end_index = 0;

    bool operator==(const WaveTcpConfig& other) const;
    bool operator!=(const WaveTcpConfig& other) const;
};

struct TelemetryRateConfig
{
    double basic_rate_hz = 10.0;
    double feature_rate_hz = 10.0;
    double waveform_rate_hz = 1.0;
    double heartbeat_rate_hz = 1.0;
    double status_rate_hz = 1.0;

    bool operator==(const TelemetryRateConfig& other) const;
    bool operator!=(const TelemetryRateConfig& other) const;
};

struct SkyConfigDiff
{
    bool epsilon_changed = false;
    bool ptb_changed = false;
    bool hmp_changed = false;
    bool lidar_changed = false;
    bool wave_tcp_changed = false;
    bool telemetry_changed = false;
};

struct SkyConfig
{
    SerialDeviceConfig epsilon;
    SerialDeviceConfig ptb;
    SerialDeviceConfig hmp;
    SerialDeviceConfig lidar;
    WaveTcpConfig wave_tcp;
    TelemetryRateConfig telemetry;

    static SkyConfig defaults();
    static bool loadFromFile(const QString& filename, SkyConfig& config, QString *errorMessage = nullptr);
    bool saveToFile(const QString& filename, QString *errorMessage = nullptr) const;
    static bool fromJson(const QJsonObject& object, SkyConfig& config, QString *errorMessage = nullptr);
    QJsonObject toJson() const;
    bool validate(QString *errorMessage = nullptr) const;
    SkyConfigDiff diff(const SkyConfig& other) const;
};

}  // namespace VaporView

#endif
