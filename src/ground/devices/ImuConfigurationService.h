#pragma once

#include "data_collector.h"
#include "LogRecord.h"

#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace VaporView::Ground::Devices
{

struct ImuProfileRequest
{
    bool english = false;
    QString port;
    QString outputFormat = QStringLiteral("HI91");
    int currentBaud = 921600;
    int targetBaud = 921600;
    int targetRateHz = 200;
};

class ImuConfigurationService final
{
public:
    struct LogEntry
    {
        VaporView::LogLevel level = VaporView::LogLevel::Info;
        QString category = QStringLiteral("device.navigation.command");
        QString event;
        QString message;
        QVariantMap fields;
    };

    using LogCallback = std::function<void(const LogEntry&)>;

    static bool isSupported(const QString& outputFormat, int rateHz);
    static bool apply(
        const ImuProfileRequest& request,
        const std::shared_ptr<ImuCollector>& collector,
        const LogCallback& log);
};

}  // namespace VaporView::Ground::Devices
