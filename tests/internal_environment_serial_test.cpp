#include "InternalEnvironmentSerialController.h"
#include "TelemetryCodec.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdlib>
#include <iostream>
static void require(bool value,const char *message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using namespace VaporView;
    SkyConfig config=SkyConfig::defaults();
    require(!config.bmp390.enabled&&!config.sht45.enabled&&config.bmp390.baud_rate==115200&&config.sht45.frequency_hz==1,
        "new bridges default to disabled optional devices with 115200 and 1 Hz");
    config.bmp390={true,"TEST-BMP",57600,2.0,{}};config.sht45={true,"TEST-SHT",115200,5.0,{}};
    SkyConfig decoded;QString error;
    require(SkyConfig::fromJson(config.toJson(),decoded,&error)&&decoded.bmp390==config.bmp390&&decoded.sht45==config.sht45,
        "bridge settings round trip independently from PTB/HMP");
    auto changed=config;changed.bmp390.baud_rate=115200;
    require(config.diff(changed).bmp390_changed&&!config.diff(changed).ptb_changed&&!config.diff(changed).sht45_changed,
        "only the changed internal collector is reconfigured");
    auto invalid=config.toJson();auto bridge=invalid.value("sht45").toObject();bridge["frequency_hz"]=1.5;invalid["sht45"]=bridge;
    require(!SkyConfig::fromJson(invalid,decoded,&error)&&decoded.sht45==config.sht45,"unsupported fractional receive rate is rejected atomically");
    auto legacy=config.toJson();legacy.remove("bmp390");legacy.remove("sht45");
    require(SkyConfig::fromJson(legacy,decoded,&error)&&!decoded.bmp390.enabled&&!decoded.sht45.enabled,"old peer configuration leaves new bridges disabled");
    for(auto id:{SkyDeviceId::Bmp390,SkyDeviceId::Sht45}) {
        SkyDeviceId parsed;require(skyDeviceIdFromValue(quint8(id),parsed)&&parsed==id,"new device IDs decode independently");
        const auto payload=TelemetryCodec::serializeDeviceCommand(id);require(TelemetryCodec::parseDeviceCommand(payload,parsed)&&parsed==id,"direct device command preserves the bridge identity");
    }
    InternalEnvironmentSerialController controller;
    controller.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_TEST_DOES_NOT_EXIST",0,1.0,{}});
    require(!controller.connectDevice(SkyDeviceId::Bmp390)&&!controller.active(),"invalid baud cannot start a bridge worker");
    controller.configure(SkyDeviceId::Bmp390,{true,"COM_VAPORVIEW_TEST_DOES_NOT_EXIST",115200,1.0,{}});
    require(controller.connectDevice(SkyDeviceId::Bmp390)&&controller.state(SkyDeviceId::Bmp390)==DeviceState::Connecting,
        "opening serial is pending rather than reported connected");
    controller.disconnectDevice(SkyDeviceId::Bmp390);
    QElapsedTimer timer;timer.start();while(timer.elapsed()<100){QCoreApplication::processEvents();QThread::msleep(1);}
    require(controller.state(SkyDeviceId::Bmp390)==DeviceState::Disconnected&&!controller.active(),
        "cancelled worker joins and cannot restore stale connection state");
}
