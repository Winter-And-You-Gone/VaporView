#include "FpgaUsbSimulator.h"
#include "FpgaDeviceController.h"
#include "FpgaSensorAdapter.h"
#include "FpgaTelemetry.h"
#include "ground/devices/RemoteSkyController.h"
#include "shared/session/HarmonicWaveform.h"
#include "shared/session/UnifiedRawDat.h"
#include <QFile>
#include "test_settings_sandbox.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace VaporView;
using namespace VaporView::Ground::Devices;
namespace
{
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class Predicate> void wait(Predicate done, const char *message, int timeout = 12000)
{
    QElapsedTimer clock;clock.start();
    while (!done() && clock.elapsed()<timeout) {QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
    check(done(),message);
}
void events(int ms) { QElapsedTimer t;t.start();wait([&]{return t.elapsed()>=ms;},"elapsed event interval",ms+1000); }
QByteArray bytes(const std::vector<std::uint8_t>& b) { return {reinterpret_cast<const char*>(b.data()),qsizetype(b.size())}; }
void protocol()
{
    auto transport=makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::RunningDemo);check(transport->open(),"simulator opens without hardware");
    check(!transport->diagnostic().hardwareBackendAvailable && transport->diagnostic().fields.value("mode")=="simulation",
          "transport truthfully identifies simulation");
    FpgaVlp1::StreamParser parser;FpgaSensor::FpgaSensorAdapter adapter;FpgaWave::Assembler assembler;
    QSet<quint16> sensors,waves;QSet<quint8> epsilonIds;EpsilonData epsilonData;
    bool bmp=false,navigation=false;
    QSet<quint32> rd105Channels;
    const auto collect=[&] {
        QByteArray b;const auto count=transport->read(b,16384);check(count>=0&&count<=503,"bounded fragmented reads");
        for(const auto& f:parser.feed(reinterpret_cast<const std::uint8_t*>(b.constData()),b.size())) {
            if(f.header.frameType!=FpgaVlp1::FrameType::Data)continue;
            if(f.header.source>=0x40) {
                const auto sample=adapter.accept(FpgaSensor::FpgaSensorDecoder::decode(bytes(f.payload),f.header.source,
                    f.header.message,f.header.flags,f.header.timestamp));
                check(sample.reading.validity.structure && sample.reading.validity.crc,"valid TLV or FDILink wire payload");
                if(sample.reading.validity.measurement)sensors.insert(f.header.source);
                if(f.header.source==0x43)bmp=sample.reading.pressurePa.has_value();
                if(f.header.source==0x48 && sample.reading.validity.measurement) {
                    const auto& r=sample.reading;
                    check(r.schema==2&&r.rd105Channel&&r.rd105OutputRaw==0&&r.rd105OutputEnabled==0,
                        "both simulated RD105 outputs start disabled with measured zero percent");
                    rd105Channels.insert(*r.rd105Channel);
                    FpgaSensor::Reading remote;
                    check(FpgaRemote::parseSensor(FpgaRemote::encodeSensor(r,100),remote)
                        &&remote.rd105Channel==r.rd105Channel&&remote.rd105OutputRaw==r.rd105OutputRaw
                        &&remote.rd105OutputEnabled==r.rd105OutputEnabled,
                        "RD105 channel and output telemetry survive remote encoding");
                }
                if(sample.epsilon) {
                    epsilonIds.insert(*sample.reading.epsilonMessageId);epsilonData=*sample.epsilon;
                    navigation|=sample.navigationValid&&sample.utcValid;
                }
            } else {
                const auto wave=assembler.accept({f.header.source,f.header.message,f.header.flags,
                    f.header.cycleId,f.header.timestamp,bytes(f.payload)});
                if(wave){check(wave->complete,"all emitted waveform fragments form a complete cycle");waves.insert(wave->source);}
            }
        }
    };
    wait([&]{collect();return sensors.size()==8&&sensors.contains(0x48)&&waves.contains(0x30)&&waves.contains(0x31)&&bmp&&navigation
        &&rd105Channels.size()==2&&epsilonIds.size()==14&&epsilonData.ecef_packet_rate_hz>0;},
         "eight sensors including RD105, calibrated BMP and valid EPSILON navigation traverse the actual decoder");
    check(epsilonIds==QSet<quint8>({0x39,0x40,0x41,0x42,0x50,0x51,0x53,0x59,0x5a,0x5c,0x5d,0x5f,0x63,0x64}),
        "all supported core EPSILON navigation messages traverse real FDILink frames");
    check(epsilonData.device_info_valid&&epsilonData.hardware_name=="EPSILON2"&&epsilonData.firmware_name=="SIM",
        "fixed-width device identity does not overlap the firmware version");
    check(epsilonData.latitude_deg>22.29&&epsilonData.latitude_deg<22.31&&epsilonData.longitude_deg>114.16
        &&epsilonData.longitude_deg<114.18&&std::abs(epsilonData.height_m+epsilonData.ned_d_m-35)<1e-5,
        "LLH and local NED share one simulated trajectory");
    check(epsilonData.gnss_velocity_valid&&epsilonData.gnss_time_valid&&epsilonData.gnss_tilt_valid
        &&epsilonData.heading_valid&&!epsilonData.floating_ambiguity_heading&&epsilonData.diff_age_s>0,
        "RTK fixed simulator supplies truthful velocity, time, heading and correction-age fields");
    check(epsilonData.pressure_altitude_m>34&&epsilonData.pressure_pa>100000&&epsilonData.imu_temp_c>23
        &&std::hypot(epsilonData.mag_x_mg,epsilonData.mag_y_mg,epsilonData.mag_z_mg)>400
        &&std::abs(epsilonData.body_vel_x_mps)>0.4&&std::abs(epsilonData.ned_acc_e_mps2)>0.01,
        "IMU environment, magnetic field, body motion and navigation acceleration are populated");
    check(epsilonData.attitude_source_count==3&&epsilonData.attitude_delta_max_deg<0.0001
        &&std::abs(epsilonData.quat_w*epsilonData.quat_w+epsilonData.quat_x*epsilonData.quat_x
            +epsilonData.quat_y*epsilonData.quat_y+epsilonData.quat_z*epsilonData.quat_z-1)<1e-6,
        "all attitude sources agree and the quaternion is normalized");
    // Invalid register access returns an error instead of silently accepting it.
    const auto request=bytes(FpgaVlp1::buildReadReg(777,0xdeac,1));
    transport->write(request.left(7));transport->write(request.mid(7));bool rejected=false;
    wait([&]{QByteArray b;transport->read(b,16384);
        for(const auto& f:parser.feed(reinterpret_cast<const std::uint8_t*>(b.constData()),b.size()))
            if(f.header.frameType==FpgaVlp1::FrameType::Response&&f.header.sequence==777)
                rejected=f.payload.size()==4&&qFromLittleEndian<quint32>(f.payload.data())==5;
        return rejected;},"partial command reads are reassembled and bad register accesses fail");
    transport->close();QByteArray b;check(transport->read(b,10)==-1,"closed simulator cannot produce stale bytes");
}
void controls()
{
    FpgaDeviceController controller;
    FpgaControlConfig desired;desired.backend="simulator";controller.setConfiguration(desired);
    check(controller.discoverDevices().value("devices").toArray().size()==1,
          "local factory discovers a simulator independently of physical USB");
    QSet<quint16> sensors,waves;QMap<quint16,int> waveCount;QString failure;bool configuredRaw=false;QObject observer;
    QObject::connect(&controller,&FpgaDeviceController::logRecordGenerated,&observer,[&](const LogRecord& r){if(r.level>=LogLevel::Error)failure=r.message;});
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,&observer,[&](const auto& r){if(r.reading.validity.measurement)sensors.insert(r.reading.source);});
    QObject::connect(&controller,&FpgaDeviceController::waveformUpdated,&observer,[&](const auto& w){if(w.complete){waves.insert(w.source);++waveCount[w.source];
        if(w.source==0x20)configuredRaw=w.totalPoints==8000&&w.signed32Samples.size()==8000&&
            std::all_of(w.signed32Samples.cbegin(),w.signed32Samples.cend(),[](qint32 v){return v==83886;});}});
    controller.connectDevice("simulation://fpga","simulator");
    const auto idle=[&]{check(failure.isEmpty(),qPrintable(failure));return controller.ready()&&!controller.busy();};
    wait(idle,"real controller handshake and connection recovery complete");
    for(quint16 source:{0x40,0x41,0x42,0x43,0x44,0x45,0x46}) {
        controller.connectSensor({{"op","sensor"},{"source",source},{"enabled",true}});wait(idle,"explicit sensor startup");
    }
    for(int ch=0;ch<2;++ch){controller.setDac(ch,true);wait(idle,"explicit DAC startup");}
    controller.setAcquisition(true);wait(idle,"explicit acquisition startup");
    wait([&]{return sensors.size()==7&&waves.size()==2;},"controller receives six sensors and both DLIA channels");
    controller.setTemperature(-2.5);wait(idle,"signed AI8 setpoint transaction completes");
    check(qint16(controller.hardwareValues().value(0x6654))==-25&&controller.hardwareValues().value(0x6648)==1,
          "AI8 setpoint confirmation and readback are observable");
    auto config=FpgaControlConfig{};config.wms[0].scanMilliHz=125000;config.dlia[0].outputRateHz=5000;
    config.backend="simulator";
    config.wms[0].scanAmplitude=0;config.wms[0].sineAmplitude=0;config.wms[0].bias=21474836;
    controller.applyConfiguration(config);wait(idle,"WMS, DAC, shared ADC and DLIA configuration commits pass");
    check(controller.hardwareValues().value(0x2010)==125000&&controller.hardwareValues().value(0x504c)==5000,
          "actual register readbacks reflect applied configuration");
    controller.setAcquisition(true);wait(idle,"acquisition starts through normal controller commands");
    controller.setRawEnabled(true);wait(idle,"RAW upload starts");
    wait([&]{return waves.contains(0x20)&&waves.contains(0x21);},"multi-fragment RAW cycles complete on both channels");
    check(configuredRaw,"applied scan frequency, amplitudes and DC bias change actual generated ADC bytes");
    controller.setWaveform(0,false);wait(idle,"channel one WMS stops independently");
    controller.setDac(0,false);wait(idle,"channel one DAC stops independently");
    check(controller.hardwareValues().value(0x2004)==0&&controller.hardwareValues().value(0x2104)==1&&
          controller.hardwareValues().value(0x3004)==0&&controller.hardwareValues().value(0x3104)==1,
          "WMS and DAC preserve channel two state");
    controller.setSensorEnabled(0x40,false);wait(idle,"PTB disable commits");
    check(controller.hardwareValues().value(0x6004)==0,"sensor control changes actual enable register");
    controller.setAcquisition(false);wait(idle,"stop and drain succeeds");events(300);const auto stopped=waveCount;
    events(300);check(waveCount==stopped,"stopped ADC and DLIA no longer emit new waves");
    controller.disconnectDevice();check(!controller.ready(),"disconnect clears readiness");
    controller.connectDevice("simulation://fpga","simulator");wait(idle,"simulator reconnect performs a fresh handshake");
    controller.disconnectDevice();
}
void selectiveControls()
{
    FpgaDeviceController controller;
    FpgaControlConfig desired;desired.backend="simulator";controller.setConfiguration(desired);
    QSet<quint16> sensors;QMap<quint16,int> sampleCounts;QVector<quint64> pingTicks;bool waveform=false;QObject observer;
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,&observer,[&](const auto& r){if(r.reading.validity.measurement){sensors.insert(r.reading.source);++sampleCounts[r.reading.source];}});
    QObject::connect(&controller,&FpgaDeviceController::waveformUpdated,&observer,[&](const auto&){waveform=true;});
    QObject::connect(&controller,&FpgaDeviceController::rawUsbBytes,&observer,[&,parser=FpgaVlp1::StreamParser{}](quint64,const QByteArray& bytes) mutable {
        for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size()))
            if(frame.header.frameType==FpgaVlp1::FrameType::Response&&frame.header.message==0xfe)pingTicks.append(frame.header.timestamp);
    });
    controller.connectDevice("simulation://fpga","simulator");
    const auto idle=[&]{return controller.ready()&&!controller.busy();};
    wait(idle,"selective simulator completes its idle-board handshake");
    for(quint32 base:{0x2000u,0x2100u,0x3000u,0x3100u,0x4000u,0x4100u,0x5000u,0x5100u,
                      0x6000u,0x6100u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u})
        check(controller.hardwareValues().value(base+4)==0,"idle simulated board starts with every sensor and acquisition/output channel stopped");
    events(250);check(sensors.isEmpty()&&!waveform,"idle simulated board emits no device measurements or waveforms");
    controller.connectSensor({{"op","sensor"},{"source",0x40},{"enabled",true}});wait(idle,"requested PTB connects");
    wait([&]{return sensors.contains(0x40);},"requested PTB produces real decoded simulator data");
    const int firstCount=sampleCounts.value(0x40);events(250);
    check(sampleCounts.value(0x40)==firstCount,"PTB polling respects its one-second register interval");
    check(sensors==QSet<quint16>{0x40}&&!waveform,"single-device connection produces only the selected source");
    controller.connectSensor({{"op","sensor"},{"source",0x44},{"enabled",true}});wait(idle,"second independent device connects");
    wait([&]{return sensors.contains(0x44);},"second source produces measurements");
    check(controller.hardwareValues().value(0x6004)==1&&controller.hardwareValues().value(0x6404)==1,
        "connecting a second simulated device preserves the first");
    controller.setSensorEnabled(0x40,false);wait(idle,"first simulated sensor disconnects independently");
    check(controller.connected()&&controller.hardwareValues().value(0x6404)==1,"individual stop keeps USB and the other sensor running");
    controller.disconnectDevice();
    controller.connectDevice("simulation://fpga","simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    wait(idle,"host reconnect reads the same simulated board");
    check(controller.hardwareValues().value(0x6004)==0&&controller.hardwareValues().value(0x6404)==1,
        "opening USB preserves stopped and running sensors instead of power-cycling the board");
    check(pingTicks.size()==2&&pingTicks.last()>pingTicks.first(),"protocol responses preserve the board timebase across host reconnects");
    controller.disconnectDevice();
}

struct Child
{
    QProcess process;
    ~Child(){if(process.state()!=QProcess::NotRunning){process.write("quit\n");process.closeWriteChannel();
        if(!process.waitForFinished(5000)){process.kill();process.waitForFinished(3000);}}}
};
void processTest()
{
    QTemporaryDir directory;check(directory.isValid(),"isolated simulator config and recording directory");
    QTcpServer portReservation;check(portReservation.listen(QHostAddress::LocalHost,0),"reserve local telemetry port");
    const auto port=portReservation.serverPort();portReservation.close();
    Child child;const auto executable=QCoreApplication::applicationDirPath()+"/VaporViewFpgaSimulator"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
    check(QFileInfo::exists(executable),"standalone simulator executable was built");
    child.process.setProcessChannelMode(QProcess::MergedChannels);
    child.process.start(executable,{"--sky-config",directory.filePath("sky.json"),"--telemetry-tcp-port",QString::number(port),"--ipc-port","0"});
    check(child.process.waitForStarted(5000),"standalone simulator starts");
    RemoteSkyController remote;QJsonObject status;QSet<quint16> sensors,waves;
    bool basic=false,position=false,fullCyclePeakReceived=false;QMap<quint32,DeviceOperationResponse> replies;
    QObject::connect(&remote,&RemoteSkyController::fpgaStatusUpdated,[&](const auto& s){status=s;});
    QObject::connect(&remote,&RemoteSkyController::fpgaSensorUpdated,[&](const auto& s){sensors.insert(s.source);});
    QObject::connect(&remote,&RemoteSkyController::fpgaWaveformUpdated,[&](const auto& s){waves.insert(s.source);
        fullCyclePeakReceived |= s.source>=0x30 && s.hasFullCyclePeak && std::isfinite(s.fullCyclePeak);});
    QObject::connect(&remote,&RemoteSkyController::basicTelemetryUpdated,[&](const auto& b){
        basic|=(b.validity_flags&BasicHasPressure)&&std::isfinite(b.pressure_hpa);
        position|=(b.validity_flags&BasicHasPosition)&&(b.validity_flags&BasicHasNedVelocity)
            &&b.latitude_deg>22.29&&b.latitude_deg<22.31&&b.longitude_deg>114.16&&b.longitude_deg<114.18&&b.height_m>34;
    });
    QObject::connect(&remote,&RemoteSkyController::deviceOperationResponseReceived,[&](const auto& response){replies.insert(response.request_id,response);});
    events(300);check(remote.openTcp("127.0.0.1",port),"Ground controller connects to the real simulator process");
    wait([&]{check(child.process.state()!=QProcess::NotRunning,"simulator stays running");return status.value("ready").toBool()&&!status.value("busy").toBool();},
         "standalone simulated board completes its cold handshake");
    check(sensors.isEmpty()&&waves.isEmpty(),"standalone service does not pre-enable devices for display");
    check(status.value("simulation").toBool(),"normal status explicitly identifies simulated measurements");
    auto operation=[&](const QJsonObject& request) {
        const auto id=remote.sendFpgaControl(request);check(id!=0,"remote FPGA operation submitted");
        wait([&]{return replies.contains(id);},"final FPGA response received");
        check(replies[id].error_code==CommandErrorCode::Ok,qPrintable(replies[id].error_message));
    };
    for(quint16 source:{0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x48})operation({{"op","sensor"},{"source",source},{"enabled",true}});
    operation({{"op","acquisition"},{"enabled",true}});
    wait([&]{return sensors.size()==8&&waves.size()==2&&basic&&position;},
        "explicit startup yields sensors, waveforms and valid EPSILON position/velocity through SkyCore telemetry");
    operation({{"op","temperature"},{"celsius",32.5}});
    operation({{"op","raw"},{"enabled",true}});
    wait([&]{return waves.contains(0x20)&&waves.contains(0x21);},"RAW previews cross process and telemetry boundaries");
    remote.sendCommand(CommandId::StartRecording);
    wait([&]{return status.value("recording_state").toInt()==1;},"recording becomes active");
    sensors.clear();waves.clear();
    wait([&]{return sensors.size()==8&&waves.size()==4&&fullCyclePeakReceived&&
        status.value("recording_statistics").toObject().value("waveform_frames").toString().toULongLong()>5;},
         "recording saves complete harmonic cycles while bounded previews carry full-cycle peaks");
    const auto session=status.value("session_directory").toString();
    check(session.startsWith(directory.path()+"/simulated_sessions"),"simulated recordings are isolated from real sessions");
    remote.sendCommand(CommandId::StopRecording);
    wait([&]{return status.value("recording_state").toInt()==0;},"recording stops and flushes the writer");
    check(!QFile::exists(session+"/raw/fpga_vlp1.dat") && !QFile::exists(session+"/sensors/fpga_frames.jsonl"),
          "new process recordings do not retain communication archives");
    QFile rawFile(session+"/raw/waveform.dat");check(rawFile.open(QIODevice::ReadOnly),"common waveform file exists");
    const auto index=SessionRawDat::scan(rawFile);check(index.success()&&index.records.size()>5,"generated harmonic records scan");
    QSet<quint16> savedChannels;
    for(const auto& record:index.records) {
        check(record.header.sourceId==5&&record.header.recordType==2,"generated record uses common harmonic type");
        check(rawFile.seek(qint64(record.payloadOffset)),"seek generated cycle");
        const auto bytes=rawFile.read(record.header.payloadSize);Session::HarmonicLayout layout;
        check(Session::parseHarmonicLayout(bytes,record.header.payloadSize,layout),"generated cycle metadata decodes");
        savedChannels.insert(layout.channel);
        check(layout.storedPoints==layout.originalPoints&&!Session::harmonicSamples(bytes,6).isEmpty(),"generated full-resolution H1/H2 can normalize on demand");
    }
    check(savedChannels==QSet<quint16>({0x30,0x31}),"both DLIA channels saved without ADC RAW");
    QFile features(session+"/sensors/waveform_features.csv");check(features.open(QIODevice::ReadOnly),"generated feature file exists");
    check(features.readAll().trimmed().split('\n').size()==index.records.size()+1,"one persisted normalized feature per generated cycle");
    operation({{"op","disconnect"}});operation({{"op","connect"},{"backend","auto"},{"locator","simulation://fpga"}});
    wait([&]{return status.value("ready").toBool();},"process reconnect keeps using the simulator backend");
    remote.close();child.process.write("quit\n");child.process.closeWriteChannel();
    check(child.process.waitForFinished(8000)&&child.process.exitStatus()==QProcess::NormalExit&&child.process.exitCode()==0,
          "standalone simulator exits normally and releases its worker");
}
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try { if(app.arguments().contains("--process"))processTest();else {protocol();controls();selectiveControls();}
        std::cout<<"FPGA simulator test passed\n";return 0; }
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
