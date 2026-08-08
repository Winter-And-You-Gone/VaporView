#pragma once

#include "LogRecord.h"

#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace VaporView
{
class EpsilonCollector;
}

namespace VaporView::Ground
{

struct EpsilonDeviceOperation
{
    QString port;
    int baud = 0;
    QString baud_text;
    bool english = false;
    std::shared_ptr<VaporView::EpsilonCollector> live_collector;
    bool restart_live_stream = false;
};

struct EpsilonConfigurationResult
{
    bool command_succeeded = false;
    bool live_stream_restarted = false;
    QString error_message;

    bool succeeded() const
    {
        return command_succeeded && live_stream_restarted;
    }
};

struct EpsilonConfigurationLogEntry
{
    VaporView::LogLevel level = VaporView::LogLevel::Info;
    QString category;
    QString event;
    QString message;
    QVariantMap fields;
};

class EpsilonConfigurationService final
{
public:
    using LogCallback = std::function<void(const EpsilonConfigurationLogEntry&)>;

    static constexpr int PacketConfigurationVersion = 2;

    static EpsilonConfigurationResult applyMainAntennaLeverArm(
        const EpsilonDeviceOperation& operation,
        double x_m,
        double y_m,
        double z_m,
        const LogCallback& log);

    static EpsilonConfigurationResult configureRtcmPort(
        const EpsilonDeviceOperation& operation,
        const QString& forward_port,
        int forward_baud,
        const QString& forward_baud_text,
        const LogCallback& log);

    static EpsilonConfigurationResult configurePacketRates(
        const EpsilonDeviceOperation& operation,
        int output_rate_hz,
        int callback_rate_hz,
        const std::map<uint8_t, int>& packet_rates,
        const QString& packet_rate_signature,
        const LogCallback& log);
};

} // namespace VaporView::Ground
