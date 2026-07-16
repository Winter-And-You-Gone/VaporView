#pragma once

#include "data_collector.h"

#include <memory>
#include <mutex>

namespace VaporView::Ground::Devices
{

struct CollectorSet
{
    std::shared_ptr<VaporView::EpsilonCollector> epsilon;
    std::shared_ptr<VaporView::GnssCollector> gnss;
    std::shared_ptr<VaporView::ImuCollector> imu;
    std::shared_ptr<VaporView::PtbCollector> ptb;
    std::shared_ptr<VaporView::HmpCollector> hmp;
    std::shared_ptr<VaporView::LidarCollector> lidar;
    std::shared_ptr<VaporView::TemperatureControllerCollector> temperature_controller;
};

class CollectorRegistry final
{
public:
    CollectorSet snapshot() const;
    void replaceAll(CollectorSet collectors, bool english);
    std::shared_ptr<VaporView::TemperatureControllerCollector> replaceTemperatureController(
        std::shared_ptr<VaporView::TemperatureControllerCollector> collector,
        bool english);
    std::shared_ptr<VaporView::TemperatureControllerCollector> takeTemperatureController();
    bool anyRunning() const;
    void setEnglish(bool english);
    void stopAll();

private:
    CollectorSet takeAll();

    mutable std::mutex mutex_;
    CollectorSet collectors_;
};

} // namespace VaporView::Ground::Devices
