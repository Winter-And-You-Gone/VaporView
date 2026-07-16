#pragma once

#include "data_collector.h"

#include <QString>

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
    using LogCallback = std::function<void(const QString&)>;

    static bool isSupported(const QString& outputFormat, int rateHz);
    static bool apply(
        const ImuProfileRequest& request,
        const std::shared_ptr<ImuCollector>& collector,
        const LogCallback& log);
};

}  // namespace VaporView::Ground::Devices
