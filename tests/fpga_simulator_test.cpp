#include "FpgaUsbSimulator.h"
#include "FpgaDeviceController.h"
#include "FpgaSensorAdapter.h"
#include "FpgaTelemetry.h"
#include "ground/devices/RemoteSkyController.h"
#include "shared/session/FpgaSessionArchive.h"
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
    auto transport=makeFpgaUsbSimulator();check(transport->open(),"simulator opens without hardware");
    check(!transport->diagnostic().hardwareBackendAvailable && transport->diagnostic().fields.value("mode")=="simulation",
          "transport truthfully identifies simulation");
    FpgaVlp1::StreamParser parser;FpgaSensor::FpgaSensorAdapter adapter;FpgaWave::Assembler assembler;
    QSet<quint16> sensors,waves;bool bmp=false,navigation=false;
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
                if(sample.epsilon)navigation|=sample.navigationValid&&sample.utcValid;
            } else {
                const auto wave=assembler.accept({f.header.source,f.header.message,f.header.flags,
                    f.header.cycleId,f.header.timestamp,bytes(f.payload)});
                if(wave){check(wave->complete,"all emitted waveform fragments form a complete cycle");waves.insert(wave->source);}
            }
        }
    };
    wait([&]{collect();return sensors.size()==7&&waves.contains(0x30)&&waves.contains(0x31)&&bmp&&navigation;},
         "seven sensors, calibrated BMP and valid EPSILON navigation traverse the actual decoder");
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
    QSet<quint16> sensors,waves;QMap<quint16,int> waveCount;QString failure;bool configuredRaw=false;
    QObject::connect(&controller,&FpgaDeviceController::logRecordGenerated,[&](const LogRecord& r){if(r.level>=LogLevel::Error)failure=r.message;});
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,[&](const auto& r){if(r.reading.validity.measurement)sensors.insert(r.reading.source);});
    QObject::connect(&controller,&FpgaDeviceController::waveformUpdated,[&](const auto& w){if(w.complete){waves.insert(w.source);++waveCount[w.source];
        if(w.source==0x20)configuredRaw=w.totalPoints==8000&&w.signed32Samples.size()==8000&&
            std::all_of(w.signed32Samples.cbegin(),w.signed32Samples.cend(),[](qint32 v){return v==83886;});}});
    controller.connectDevice("simulation://fpga","simulator");
    const auto idle=[&]{check(failure.isEmpty(),qPrintable(failure));return controller.ready()&&!controller.busy();};
    wait(idle,"real controller handshake and connection recovery complete");
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
    RemoteSkyController remote;QJsonObject status;QSet<quint16> sensors,waves;bool basic=false;QMap<quint32,DeviceOperationResponse> replies;
    QObject::connect(&remote,&RemoteSkyController::fpgaStatusUpdated,[&](const auto& s){status=s;});
    QObject::connect(&remote,&RemoteSkyController::fpgaSensorUpdated,[&](const auto& s){sensors.insert(s.source);});
    QObject::connect(&remote,&RemoteSkyController::fpgaWaveformUpdated,[&](const auto& s){waves.insert(s.source);});
    QObject::connect(&remote,&RemoteSkyController::basicTelemetryUpdated,[&](const auto& b){basic|=(b.validity_flags&BasicHasPressure)&&std::isfinite(b.pressure_hpa);});
    QObject::connect(&remote,&RemoteSkyController::deviceOperationResponseReceived,[&](const auto& response){replies.insert(response.request_id,response);});
    events(300);check(remote.openTcp("127.0.0.1",port),"Ground controller connects to the real simulator process");
    wait([&]{check(child.process.state()!=QProcess::NotRunning,"simulator stays running");return status.value("ready").toBool()&&!status.value("busy").toBool()&&sensors.size()==7&&waves.size()==2&&basic;},
         "normal Ground remote stream receives FPGA status, all sensors, waveform previews and physical telemetry");
    check(status.value("simulation").toBool(),"normal status explicitly identifies simulated measurements");
    auto operation=[&](const QJsonObject& request) {
        const auto id=remote.sendFpgaControl(request);check(id!=0,"remote FPGA operation submitted");
        wait([&]{return replies.contains(id);},"final FPGA response received");
        check(replies[id].error_code==CommandErrorCode::Ok,qPrintable(replies[id].error_message));
    };
    operation({{"op","temperature"},{"celsius",32.5}});
    operation({{"op","raw"},{"enabled",true}});
    wait([&]{return waves.contains(0x20)&&waves.contains(0x21);},"RAW previews cross process and telemetry boundaries");
    remote.sendCommand(CommandId::StartRecording);
    wait([&]{return status.value("recording_state").toInt()==1&&status.value("archived_records").toString().toULongLong()>400;},
         "recording archives real generated VLP frames, OUT commands and USB chunks");
    const auto session=status.value("session_directory").toString();
    check(session.startsWith(directory.path()+"/simulated_sessions"),"simulated recordings are isolated from real sessions");
    remote.sendCommand(CommandId::StopRecording);
    wait([&]{return status.value("recording_state").toInt()==0;},"recording stops and flushes the writer");
    const auto rawFile=session+"/raw/fpga_vlp1.dat";
    check(QFileInfo(rawFile).size()>0,"VLP archive exists on disk");
    QSet<quint16> archivedSources;QString archiveError;
    check(Session::FpgaSessionArchive::replay(session,[&](quint64,const FpgaVlp1::Frame& frame) {
        if(frame.header.frameType==FpgaVlp1::FrameType::Data)archivedSources.insert(frame.header.source);
        return true;
    },&archiveError),qPrintable(archiveError));
    check(archivedSources.size()==11&&archivedSources.contains(0x41),"saved session replays seven sensor sources including HMP and four wave sources");
    check(Session::FpgaSessionArchive::exportTo(session,directory.filePath("simulated.bin"),
        Session::FpgaSessionArchive::ExportFormat::Bin,&archiveError),qPrintable(archiveError));
    check(QFileInfo(directory.filePath("simulated.bin")).size()>0,"existing BIN export consumes the generated USB archive");
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
    try { if(app.arguments().contains("--process"))processTest();else {protocol();controls();}
        std::cout<<"FPGA simulator test passed\n";return 0; }
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
