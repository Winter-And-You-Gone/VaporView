#pragma once

#include "SkyConfig.h"
#include "TelemetryTypes.h"
#include "FpgaSensorDecoder.h"
#include "LogRecord.h"
#include <QObject>
#include <memory>

namespace VaporView {

// Independent USB serial text bridges. These collectors never feed the PTB/HMP
// environmental slots or their session/raw callbacks.
class InternalEnvironmentSerialController final : public QObject
{
    Q_OBJECT
public:
    explicit InternalEnvironmentSerialController(QObject *parent=nullptr);
    ~InternalEnvironmentSerialController() override;
    void configure(SkyDeviceId id, const SerialDeviceConfig& config);
    bool connectDevice(SkyDeviceId id);
    void disconnectDevice(SkyDeviceId id);
    void disconnectAll();
    DeviceState state(SkyDeviceId id) const;
    bool active() const;
signals:
    void stateChanged(VaporView::SkyDeviceId id, VaporView::DeviceState state);
    void readingUpdated(const VaporView::FpgaSensor::Reading& reading);
    void logRecordGenerated(const VaporView::LogRecord& record);
private:
    struct Channel;
    std::unique_ptr<Channel> bmp_, sht_;
    Channel *channel(SkyDeviceId id) const;
    void changeState(SkyDeviceId id, DeviceState state);
    void reportFailure(SkyDeviceId id, const QString& reason);
};
}
