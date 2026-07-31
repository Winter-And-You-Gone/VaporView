#include "ground/devices/CollectorRegistry.h"

#include <utility>

namespace VaporView::Ground::Devices
{
namespace
{
template <typename Collector>
void applyLanguage(const std::shared_ptr<Collector>& collector, bool english)
{
    if (collector)
    {
        collector->setEnglish(english);
    }
}

template <typename Collector>
void stopCollector(const std::shared_ptr<Collector>& collector)
{
    if (collector)
    {
        collector->stop();
    }
}
} // namespace

CollectorSet CollectorRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return collectors_;
}

void CollectorRegistry::replaceAll(CollectorSet collectors, bool english)
{
    applyLanguage(collectors.epsilon, english);
    applyLanguage(collectors.gnss, english);
    applyLanguage(collectors.imu, english);
    applyLanguage(collectors.ptb, english);
    applyLanguage(collectors.hmp, english);
    applyLanguage(collectors.lidar, english);
    applyLanguage(collectors.temperature_controller, english);
    applyLanguage(collectors.ai8_temperature_controller, english);

    std::lock_guard<std::mutex> lock(mutex_);
    collectors_ = std::move(collectors);
}

std::shared_ptr<VaporView::TemperatureControllerCollector>
CollectorRegistry::replaceTemperatureController(
    std::shared_ptr<VaporView::TemperatureControllerCollector> collector,
    bool english)
{
    applyLanguage(collector, english);
    std::lock_guard<std::mutex> lock(mutex_);
    std::shared_ptr<VaporView::TemperatureControllerCollector> previous =
        std::move(collectors_.temperature_controller);
    collectors_.temperature_controller = std::move(collector);
    return previous;
}

std::shared_ptr<VaporView::TemperatureControllerCollector>
CollectorRegistry::takeTemperatureController()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(collectors_.temperature_controller);
}

bool CollectorRegistry::anyRunning() const
{
    const CollectorSet current = snapshot();
    return (current.epsilon && current.epsilon->isRunning()) ||
           (current.gnss && current.gnss->isRunning()) ||
           (current.imu && current.imu->isRunning()) ||
           (current.ptb && current.ptb->isRunning()) ||
           (current.hmp && current.hmp->isRunning()) ||
           (current.lidar && current.lidar->isRunning()) ||
           (current.temperature_controller && current.temperature_controller->isRunning()) ||
           (current.ai8_temperature_controller && current.ai8_temperature_controller->isRunning());
}

void CollectorRegistry::setEnglish(bool english)
{
    const CollectorSet current = snapshot();
    applyLanguage(current.epsilon, english);
    applyLanguage(current.gnss, english);
    applyLanguage(current.imu, english);
    applyLanguage(current.ptb, english);
    applyLanguage(current.hmp, english);
    applyLanguage(current.lidar, english);
    applyLanguage(current.temperature_controller, english);
    applyLanguage(current.ai8_temperature_controller, english);
}

CollectorSet CollectorRegistry::takeAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    CollectorSet current = std::move(collectors_);
    collectors_ = CollectorSet{};
    return current;
}

void CollectorRegistry::stopAll()
{
    const CollectorSet current = takeAll();
    stopCollector(current.epsilon);
    stopCollector(current.gnss);
    stopCollector(current.imu);
    stopCollector(current.ptb);
    stopCollector(current.hmp);
    stopCollector(current.lidar);
    stopCollector(current.temperature_controller);
    stopCollector(current.ai8_temperature_controller);
}

} // namespace VaporView::Ground::Devices
