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
    require(parseControl(encodeControl({{"op","discover"}}),parsed),"read-only discovery operation round trip");
    require(!validateControl({{"op","discover"},{"address",42}}),"discovery cannot carry a register operation");
    const QJsonObject connect{{"op","connect"},{"locator","mock"},{"backend","auto"}};
    require(parseControl(encodeControl(connect),parsed)&&!parsed.contains("recover"),"legacy connection requests keep their recovery default");
    for(bool recover:{false,true}) {
        auto scoped=connect;scoped["recover"]=recover;
        require(parseControl(encodeControl(scoped),parsed)&&parsed.value("recover").toBool()==recover,"connection recovery policy survives remote encoding");
    }
    auto invalidConnect=connect;invalidConnect["recover"]="false";
    require(!validateControl(invalidConnect),"connection recovery policy requires a boolean");
    require(!validateControl({{"op","refresh"},{"recover",false}}),"recovery policy is limited to connection requests");
    const QJsonObject configure{{"op","configure"},{"configuration",FpgaControlConfig{}.toJson()}};
    require(parseControl(encodeControl(configure),parsed),"full configuration round trip");
    require(!configure.value("configuration").toObject().contains("rawWindowSeconds"),"remote configuration does not publish a RAW duration");
    auto legacyConfigure=configure;auto legacyConfig=FpgaControlConfig{}.toJson();legacyConfig.insert("rawWindowSeconds",10);
    legacyConfigure.insert("configuration",legacyConfig);
    require(parseControl(encodeControl(legacyConfigure),parsed)&&!FpgaControlConfig::fromJson(parsed.value("configuration").toObject()).toJson().contains("rawWindowSeconds"),"legacy remote RAW duration is accepted and discarded");
    legacyConfigure.insert("section","wms");require(validateControl(legacyConfigure),"scoped legacy configuration remains compatible");
    legacyConfig.insert("rawWindowSeconds",1.5);legacyConfigure.insert("configuration",legacyConfig);
    require(!validateControl(legacyConfigure),"malformed legacy duration still fails remote validation");
    for(const QString section:{QString("wms"),QString("dlia"),QString("acquisition"),QString("all")}) {
        auto scoped=configure;scoped["section"]=section;
        require(parseControl(encodeControl(scoped),parsed)&&parsed.value("section")==section,"configuration scope survives remote codec");
    }
    auto invalidScope=configure;invalidScope["section"]="unknown";
    require(!validateControl(invalidScope),"unknown scope rejected");
    invalidScope["section"]=1;require(!validateControl(invalidScope),"non-string scope rejected");
    auto bad=configure;auto config=bad.value("configuration").toObject(); config.insert("adcRateHz",1.5);bad.insert("configuration",config);
    require(!validateControl(bad),"fractional register value rejected before coercion");
    require(validateControl({{"op","sensor"},{"source",0x41},{"enabled",true}}),"HMP source is supported");
    require(parseControl(encodeControl({{"op","sensor_config"},{"source",0x40},{"baud",4800},{"poll_ms",250}}),parsed),"typed PTB settings survive remote transport");
    require(parseControl(encodeControl({{"op","sensor"},{"source",0x40},{"enabled",true},{"baud",4800},{"poll_ms",250}}),parsed),
        "sensor connect carries staged parameters through the typed remote contract");
    require(!validateControl({{"op","sensor"},{"source",0x40},{"enabled",false},{"baud",4800}}),"disconnect cannot apply parameters");
    require(!validateControl({{"op","sensor"},{"source",0x44},{"enabled",true},{"baud",4800}}),"connect rejects unsupported parameters");
    for(const auto &request:QVector<QJsonObject>{
        {{"op","sensor_config"},{"source",0x43},{"baud",9600}},
        {{"op","sensor_config"},{"source",0x46},{"baud",19200}},
        {{"op","sensor_config"},{"source",0x42},{"poll_ms",1000}},
        {{"op","sensor_config"},{"source",0x40},{"baud",115200}},
        {{"op","sensor_config"},{"source",0x45},{"baud",9600}},
        {{"op","sensor_config"},{"source",0x44},{"poll_ms",0}},
        {{"op","sensor_config"},{"source",0x46},{"poll_ms",3600001}},
        {{"op","sensor_config"},{"source",0x44},{"poll_ms",1.5}},
        {{"op","sensor_config"},{"source",0x40}},
        {{"op","sensor_config"},{"source",0x42},{"baud",921600},{"address",0x6210}}})
        require(!validateControl(request),"unsupported, invalid or arbitrary sensor settings rejected");
    require(!validateControl({{"op","dac"},{"channel",2},{"enabled",true}}),"channel out of range rejected");
    require(!validateControl({{"op","raw"},{"enabled",1}}),"boolean coercion rejected");
    require(!validateControl({{"op","temperature"},{"celsius",25.01}}),"temperature precision rejected");
    require(!validateControl({{"op","refresh"},{"address",42}}),"arbitrary register fields rejected");
    require(encodeControl({{"op","write_register"},{"value",3}}).isEmpty(),"raw register operation rejected");
    for(const auto& operation:QVector<QJsonObject>{
        {{"op","sensor"},{"source",0x41},{"enabled",true},{"poll_ms",1000},{"parameters",QJsonObject{{"address",240},{"measurements",3}}}},
        {{"op","sensor_config"},{"source",0x43},{"parameters",QJsonObject{{"address",118},{"oversampling",45},{"iir",14}}}},
        {{"op","time"},{"action",2},{"value",0}},{{"op","motor"},{"action",1},{"value",-100}},
        {{"op","motor_config"},{"frequency",1000},{"high_ticks",255},{"dir_ticks",505},{"polarity",0}},
        {{"op","channel_acquisition"},{"channel",1},{"enabled",true}},
        {{"op","stream_config"},{"mode",1},{"high_water",4096},{"burst",8}},
        {{"op","watchdog"},{"value",30000}},{{"op","maintenance"},{"source",2},{"action",9},{"value",0}}})
        require(parseControl(encodeControl(operation),parsed),"advanced typed operations roundtrip for Local/SkyCore");
    for(const auto& operation:QVector<QJsonObject>{
        {{"op","sensor_config"},{"source",0x43},{"parameters",QJsonObject{{"iir",3}}}},
        {{"op","sensor_config"},{"source",0x41},{"parameters",QJsonObject{{"address",0}}}},
        {{"op","sensor_config"},{"source",0x44},{"parameters",QJsonObject{{"arbitrary",1}}}},
        {{"op","time"},{"action",9},{"value",0}},{{"op","watchdog"},{"value",100}},
        {{"op","motor_config"},{"frequency",196078},{"high_ticks",256},{"dir_ticks",505},{"polarity",0}},
        {{"op","motor"},{"action",3},{"value",1}}})require(!validateControl(operation),"unsafe or unknown advanced parameters rejected");
    QByteArray event(32,0);event[0]=1;
    require(!decodeManagementEvent(0x11,0x47,0x1300,42,event).isEmpty(),"motor event decoded");
    require(decodeManagementEvent(0x11,1,0x1300,42,event).isEmpty() && decodeManagementEvent(0x11,0x47,0x1300,42,event.left(31)).isEmpty(),"event source and size validated");
    DeviceOperationRequest request;request.request_id=42;request.device_id=SkyDeviceId::Fpga;request.operation=DeviceOperation::FpgaControl;request.payload=encodeControl(configure);
    DeviceOperationRequest decoded;
    require(TelemetryCodec::parseDeviceOperationRequest(TelemetryCodec::serializeDeviceOperationRequest(request),decoded) &&
        decoded.device_id==SkyDeviceId::Fpga && decoded.operation==DeviceOperation::FpgaControl,"FPGA operation codec accepted");
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},{"configuration",FpgaControlConfig{}.toJson()},
        {"registers",QJsonObject{{"4096",4294967295.0}}},{"archived_records","18446744073709551615"}};
    require(parseStatus(encodeStatus(status),parsed),"uint64 archive precision survives status");
    status["recording_statistics"]=QJsonObject{{"data_records",QJsonObject{{"waveform","123"},{"64","42"}}},
        {"waveform_frames","12"},{"waveform_points",100},{"waveform_source",48}};
    require(parseStatus(encodeStatus(status),parsed) && parsed.value("recording_statistics")==status.value("recording_statistics"),
        "FPGA waveform and device recording counters survive remote status codec");
    auto legacyStatus=status;legacyConfig.insert("rawWindowSeconds",10);legacyStatus.insert("configuration",legacyConfig);
    require(parseStatus(encodeStatus(legacyStatus),parsed),"status from older peers accepts the obsolete RAW field");
    status["usb_discovery"]=QJsonObject{{"devices",QJsonArray{QJsonObject{{"locator","opaque-path"},{"label","GP01"},
        {"driver","CYUSB3"},{"backend","cypress"},{"available",false}}}},{"error",""}};
    require(parseStatus(encodeStatus(status),parsed) && parsed.value("usb_discovery")==status.value("usb_discovery"),"device list survives remote status codec");
    auto malformed=status;malformed["usb_discovery"]=QJsonObject{{"devices",QJsonArray{42}},{"error",""}};
    require(!parseStatus(encodeStatus(malformed),parsed),"malformed discovery result rejected");
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
