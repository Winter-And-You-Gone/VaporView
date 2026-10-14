#include "InternalEnvironmentSerialController.h"
#include "data_collector.h"
#include "SerialBaudRateCapabilities.h"
#include <atomic>
#include <cmath>
#include <thread>

namespace VaporView {
struct InternalEnvironmentSerialController::Channel {
    SerialDeviceConfig config{false,{},115200,1.0,{}};
    DeviceState state=DeviceState::Disconnected;
    std::atomic<bool> cancel{false};
    quint64 generation=0;
    std::shared_ptr<DataCollector> collector;
    std::thread worker;
};
InternalEnvironmentSerialController::InternalEnvironmentSerialController(QObject *parent)
    : QObject(parent),bmp_(std::make_unique<Channel>()),sht_(std::make_unique<Channel>()) {}
InternalEnvironmentSerialController::~InternalEnvironmentSerialController() { QObject::disconnect(this,nullptr,nullptr,nullptr);disconnectAll(); }
InternalEnvironmentSerialController::Channel *InternalEnvironmentSerialController::channel(SkyDeviceId id) const
{
    return id==SkyDeviceId::Bmp390?bmp_.get():id==SkyDeviceId::Sht45?sht_.get():nullptr;
}
void InternalEnvironmentSerialController::changeState(SkyDeviceId id, DeviceState state)
{
    auto *c=channel(id);
    if(c&&c->state!=state){c->state=state;emit stateChanged(id,state);}
}
DeviceState InternalEnvironmentSerialController::state(SkyDeviceId id) const
{ auto *c=channel(id);return c?c->state:DeviceState::Disabled; }
bool InternalEnvironmentSerialController::active() const
{
    for(auto *c:{bmp_.get(),sht_.get()})
        if(c->state==DeviceState::Connecting||c->state==DeviceState::Connected)return true;
    return false;
}
void InternalEnvironmentSerialController::reportFailure(SkyDeviceId id,const QString& reason)
{
    LogRecord record;record.level=LogLevel::Warning;record.category=QStringLiteral("device.internal");
    record.message=QStringLiteral("内部环境串口桥连接失败：%1。").arg(reason);
    record.fields={{"event","internal_serial_error"},{"device_id",skyDeviceIdName(id)},{"reason",reason},{"reason_code","SERIAL_BRIDGE_CONNECT_FAILED"}};
    emit logRecordGenerated(record);
}
void InternalEnvironmentSerialController::configure(SkyDeviceId id,const SerialDeviceConfig& config)
{
    auto *c=channel(id);if(!c)return;
    if(c->config!=config){disconnectDevice(id);c->config=config;}
}
void InternalEnvironmentSerialController::disconnectDevice(SkyDeviceId id)
{
    auto *c=channel(id);if(!c)return;
    ++c->generation;c->cancel=true;
    if(c->worker.joinable())c->worker.join();
    if(c->collector)c->collector->stop();
    c->collector.reset();changeState(id,DeviceState::Disconnected);
}
void InternalEnvironmentSerialController::disconnectAll()
{ disconnectDevice(SkyDeviceId::Bmp390);disconnectDevice(SkyDeviceId::Sht45); }
bool InternalEnvironmentSerialController::connectDevice(SkyDeviceId id)
{
    auto *c=channel(id);if(!c||c->state==DeviceState::Connecting||c->state==DeviceState::Connected)return false;
    const auto config=c->config;
    const auto &baud=id==SkyDeviceId::Bmp390?bmp390SerialAdapterBaudCapabilities():sht45SerialAdapterBaudCapabilities();
    if(!config.enabled||config.port.trimmed().isEmpty()||!std::isfinite(config.frequency_hz)
        ||config.frequency_hz<1||config.frequency_hz>20||std::floor(config.frequency_hz)!=config.frequency_hz
        ||!isBaudRateSupported(baud,config.baud_rate)) {
        changeState(id,DeviceState::Error);reportFailure(id,QStringLiteral("串口或接收频率参数无效"));return false;
    }
    disconnectDevice(id);c->cancel=false;
    const auto generation=c->generation;
    const bool pressure=id==SkyDeviceId::Bmp390;
    if(pressure){auto p=std::make_shared<PtbCollector>();p->setProtocol(PressureSensorProtocol::Bmp390Serial);c->collector=p;}
    else{auto p=std::make_shared<HmpCollector>();p->setProtocol(HumiditySensorProtocol::Sht45Serial);c->collector=p;}
    auto collector=c->collector;
    collector->setCancelCallback([c]{return c->cancel.load();});
    collector->setSampleRate(int(config.frequency_hz));
    collector->setDataCallback([this,c,id,generation,pressure,ptr=collector.get()] {
        FpgaSensor::Reading reading;reading.source=pressure?0x43:0x44;
        reading.kind=pressure?FpgaSensor::SensorKind::Bmp390:FpgaSensor::SensorKind::Sht45;
        bool valid;
        if(pressure){const auto data=static_cast<PtbCollector*>(ptr)->getLatestData();valid=data.valid&&std::isfinite(data.pressure_hpa)&&data.pressure_hpa>0;
            if(valid)reading.pressurePa=data.pressure_hpa*100;}
        else{const auto data=static_cast<HmpCollector*>(ptr)->getLatestData();valid=data.valid&&std::isfinite(data.temperature)&&std::isfinite(data.humidity)&&data.humidity>=0&&data.humidity<=100;
            if(valid){reading.temperatureC=data.temperature;reading.humidityPct=data.humidity;}}
        // Text bridges have no wire CRC; validity here means a complete parsed
        // record, rather than claiming an I2C/USB CRC was checked by the host.
        reading.validity={true,true,true,valid};
        QMetaObject::invokeMethod(this,[this,c,id,generation,reading] {
            if(c->generation==generation&&c->state==DeviceState::Connected)emit readingUpdated(reading);
        },Qt::QueuedConnection);
    });
    changeState(id,DeviceState::Connecting);
    c->worker=std::thread([this,c,id,generation,collector,config] {
        const bool ok=collector->start(config.port.toStdString(),SerialConfig::N81(config.baud_rate))
            &&!c->cancel&&(id==SkyDeviceId::Bmp390
                ?static_cast<PtbCollector*>(collector.get())->checkDeviceResponse()
                :static_cast<HmpCollector*>(collector.get())->checkDeviceResponse())
            &&!c->cancel&&collector->startStreaming();
        QString error=QString::fromStdString(collector->getLastError());
        if(!ok) {
            if(error.isEmpty())error=QStringLiteral("未收到有效串口文本；请核对桥程序、串口和波特率。");
            collector->stop();
        }
        QMetaObject::invokeMethod(this,[this,c,id,generation,ok,error] {
            if(c->generation!=generation)return;
            changeState(id,ok?DeviceState::Connected:DeviceState::Error);
            if(!ok)reportFailure(id,error);
        },Qt::QueuedConnection);
    });
    return true;
}
}
