#include "FpgaTelemetry.h"
#include "FpgaControlConfig.h"
#include "TelemetryCodec.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    using namespace VaporView;
    using namespace FpgaRemote;
    QJsonObject parsed;
    const QJsonObject configure{{"op","configure"},{"configuration",FpgaControlConfig{}.toJson()}};
    require(parseControl(encodeControl(configure),parsed),"full configuration round trip");
    auto bad=configure;auto config=bad.value("configuration").toObject(); config.insert("adcRateHz",1.5);bad.insert("configuration",config);
    require(!validateControl(bad),"fractional register value rejected before coercion");
    require(!validateControl({{"op","sensor"},{"source",0x41},{"enabled",true}}),"unknown source rejected");
    require(!validateControl({{"op","dac"},{"channel",2},{"enabled",true}}),"channel out of range rejected");
    require(!validateControl({{"op","raw"},{"enabled",1}}),"boolean coercion rejected");
    require(!validateControl({{"op","temperature"},{"celsius",25.01}}),"temperature precision rejected");
    require(!validateControl({{"op","refresh"},{"address",42}}),"arbitrary register fields rejected");
    require(encodeControl({{"op","write_register"},{"value",3}}).isEmpty(),"raw register operation rejected");
    DeviceOperationRequest request;request.request_id=42;request.device_id=SkyDeviceId::Fpga;request.operation=DeviceOperation::FpgaControl;request.payload=encodeControl(configure);
    DeviceOperationRequest decoded;
    require(TelemetryCodec::parseDeviceOperationRequest(TelemetryCodec::serializeDeviceOperationRequest(request),decoded) &&
        decoded.device_id==SkyDeviceId::Fpga && decoded.operation==DeviceOperation::FpgaControl,"FPGA operation codec accepted");
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},{"configuration",FpgaControlConfig{}.toJson()},
        {"registers",QJsonObject{{"4096",4294967295.0}}},{"archived_records","18446744073709551615"}};
    require(parseStatus(encodeStatus(status),parsed),"uint64 archive precision survives status");
    status.insert("archived_records",9007199254740992.0);
    require(!parseStatus(encodeStatus(status),parsed),"numeric uint64 archive rejected");
    FpgaSensor::Reading sensor;sensor.source=0x43;sensor.message=0x1100;sensor.schema=1;sensor.timestamp=0xffffffffffffffffULL;
    sensor.pressurePa=101325.25;sensor.temperatureC=23.5;sensor.validity={true,true,true,true,false,5};
    FpgaSensor::Reading reading;quint64 host=0;
    require(parseSensor(encodeSensor(sensor,0xffffffffffffffffULL),reading,&host) && host==0xffffffffffffffffULL &&
        reading.timestamp==sensor.timestamp && reading.pressurePa==sensor.pressurePa,"compensated BMP values and uint64 ticks survive");
    FpgaWave::CompletedStream wave;wave.source=0x20;wave.message=0x1000;wave.schema=2;wave.format=1;wave.rate=1000000;
    wave.totalPoints=1000;wave.bytesPerPoint=4;wave.adcBits=24;wave.timestamp=0xffffffffffffffffULL;wave.complete=true;
    for(int i=0;i<1000;++i)wave.unsigned32Samples.append(0xffffffffu-quint32(i));
    FpgaWave::CompletedStream preview;
    require(parseWaveformPreview(encodeWaveformPreview(wave),preview),"raw uint32 preview round trip");
    require(preview.preview && preview.previewStride==16 && preview.totalPoints==1000 && preview.rate==wave.rate &&
        preview.unsigned32Samples.size()==63 && preview.unsigned32Samples[0]==0xffffffffu && preview.pointBytes.size()==63*4,
        "bounded preview preserves native unsigned codes and original coordinates");
    FpgaWave::CompletedStream encodedAgain;
    require(parseWaveformPreview(encodeWaveformPreview(preview),encodedAgain) &&
        encodedAgain.previewStride==16 && encodedAgain.totalPoints==1000 &&
        encodedAgain.unsigned32Samples==preview.unsigned32Samples && encodedAgain.rate==preview.rate,
        "encoding a worker-prepared preview preserves original stride and samples");
    wave.source=0x30;wave.message=0x1001;wave.schema=2;wave.format=2;wave.bytesPerPoint=24;wave.adcBits=0;wave.totalPoints=1;
    wave.unsigned32Samples.clear();wave.dliaPoints.append({-1,2,-3,4,-5,6,true,true});
    require(parseWaveformPreview(encodeWaveformPreview(wave),preview) && preview.dliaPoints[0].i1==-1 &&
        preview.dliaPoints[0].h2==6 && preview.pointBytes.size()==24,"DLIA preview preserves all components");
    auto oversized=QJsonDocument::fromJson(encodeWaveformPreview(wave)).object();QJsonArray points;
    for(int i=0;i<65;++i)points.append(0);oversized.insert("points",points);
    require(!parseWaveformPreview(QJsonDocument(oversized).toJson(),preview),"oversized preview rejected");
    std::cout << "fpga_telemetry_test passed\n";
}
