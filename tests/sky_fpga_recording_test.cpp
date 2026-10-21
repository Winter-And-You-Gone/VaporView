#include "SkySessionRecorder.h"
#include "FailingRecordingStorage.h"
#include "FpgaHarmonics.h"
#include "FpgaDeviceController.h"
#include "FpgaDeviceSession.h"
#include "FpgaUsbSimulator.h"
#include "FpgaTelemetry.h"
#include "ground/session/GroundRecordingService.h"
#include "ground/session/SessionWaveformRepository.h"
#include "shared/session/HarmonicWaveform.h"
#include "shared/session/UnifiedRawDat.h"
#include "shared/session/SessionPackageLayout.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <iostream>
#include <cmath>

using namespace VaporView;
namespace {
void require(bool ok, const char *message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
quint64 nowUs() { return quint64(QDateTime::currentMSecsSinceEpoch()) * 1000; }
FpgaWave::CompletedStream wave(quint16 channel=0x30, quint16 format=0, quint32 cycle=1, int points=4) {
    FpgaWave::CompletedStream s;
    s.source=channel; s.message=0x1001; s.schema=2; s.format=format; s.cycleId=cycle;
    s.rate=10000; s.totalPoints=points; s.bytesPerPoint=format==0?8:format==1?16:24;
    s.timestamp=0xfedcba9876543210ULL; s.flags=3; s.complete=true;
    for(int i=0;i<points;++i) {
        FpgaWave::CompletedStream::DliaPoint p;
        p.hasHarmonics=format!=1; p.hasIq=format!=0;
        p.h1=i==2?0:i==0?16777217:2; p.h2=i==2?999999:i==0?16777218:20;
        p.i1=3; p.q1=4; p.i2=5; p.q2=12;
        s.dliaPoints.append(p);
    }
    return s;
}
QByteArray read(const QString& path) { QFile file(path); require(file.open(QIODevice::ReadOnly),"read recorded file"); return file.readAll(); }
SessionRawDat::RawScanResult scan(const QString& directory) {
    QFile file(QDir(directory).filePath("raw/waveform.dat")); require(file.open(QIODevice::ReadOnly),"open common waveform file");
    return SessionRawDat::scan(file);
}
void checkPackage(const QString& directory, int count) {
    require(!QFile::exists(QDir(directory).filePath("raw/fpga_vlp1.dat")),"no protocol archive");
    require(!QFile::exists(QDir(directory).filePath("sensors/fpga_frames.jsonl")),"no duplicate FPGA semantic archive");
    const auto index=scan(directory);
    require(index.success() && index.records.size()==count,"all admitted cycles reach the shared waveform file");
    for(const auto& r:index.records) require(r.header.sourceId==5 && r.header.recordType==2,"waveforms use the common source and typed harmonic payload");
    const auto manifest=QJsonDocument::fromJson(read(QDir(directory).filePath("session.json"))).object();
    require(manifest.value("counts").toObject().value("waveform_frames").toString().toInt()==count,"manifest counts cycles once");
    require(manifest.value("counts").toObject().value("waveform_feature_rows").toString().toInt()==count,"one durable feature per cycle");
    require(!manifest.value("raw_files").toObject().contains("fpga_vlp1"),"manifest does not advertise removed archive");
    require(manifest.value("waveform_processing").toObject().value("algorithm_version").toInt()==1,"normalization rule is traceable");
    require(read(QDir(directory).filePath("sensors/waveform_features.csv")).trimmed().split('\n').size()==count+1,"feature csv includes every cycle");
}
void commonFormatAndPreview() {
    auto s=wave();
    const auto payload=Session::encodeHarmonics(s,9);
    Session::HarmonicLayout l;
    require(Session::parseHarmonicLayout(payload,quint32(payload.size()),l),"typed layout parses");
    require(l.segment==9 && l.fpgaTick==s.timestamp && l.storedPoints==4 && l.sampleType==Session::HarmonicSampleType::Int32,"timing and integer metadata preserved");
    require(qFromLittleEndian<qint32>(payload.data()+Session::kHarmonicHeaderSize)==16777217,"native precision survives values beyond float32 integer range");
    require(payload.size()==48+4*8,"only H1 and H2 stored, without protocol or normalized samples");
    auto ratio=Session::harmonicSamples(payload,6);
    require(ratio.size()==4 && std::isnan(ratio[2]) && ratio[3]==10,"ratio uses same-point values and keeps invalid reference gaps");
    auto iq=Session::encodeHarmonics(wave(0x31,1),10);
    require(Session::parseHarmonicLayout(iq,quint32(iq.size()),l) && l.sampleType==Session::HarmonicSampleType::Float64,"IQ-only amplitude stored as float64");
    require(std::abs(Session::harmonicSamples(iq,6)[0]-2.6f)<1e-6f,"IQ normalization agrees with amplitude normalization");
    auto withIq=Session::encodeHarmonics(wave(0x31,2),11);
    require(withIq.size()==payload.size(),"combined format does not archive unused IQ words");
    auto bad=payload; qToLittleEndian<quint32>(99,bad.data()+20);
    require(!Session::parseHarmonicLayout(bad,quint32(bad.size()),l),"inconsistent counts rejected");
    auto large=wave(0x30,0,5,130);
    for(auto& p:large.dliaPoints) {p.h1=2;p.h2=2;}
    large.dliaPoints[1].h2=198;
    large.hasFullCyclePeak=true; large.fullCyclePeak=FpgaWave::normalizedCyclePeak(large);
    FpgaWave::CompletedStream preview;
    require(FpgaRemote::parseWaveformPreview(FpgaRemote::encodeWaveformPreview(large),preview),"preview metadata roundtrip");
    require(preview.preview && preview.dliaPoints.size()<=64 && preview.hasFullCyclePeak && preview.fullCyclePeak==99,"full-cycle peak survives even when thinning omits its sample");
    require(preview.dliaPoints[0].h2==2 && preview.dliaPoints[1].h2==2,"fixture genuinely loses the peak point during preview thinning");
}
void groundRoundTrip() {
    using namespace Ground::Session;
    QTemporaryDir temporary; GroundRecordingService recorder; GroundRecordingOptions options;
    options.baseDirectory=temporary.path(); options.fpgaConfiguration=FpgaControlConfig{}.toJson();
    require(recorder.start(options),"ground start"); const auto directory=recorder.status().sessionDirectory;
    require(recorder.recordFpgaSnapshot(nowUs(),{{"event","connected"}}),"ground configuration boundary");
    require(recorder.recordFpgaHarmonics(nowUs(),wave()),"ground H1 H2");
    require(recorder.recordFpgaHarmonics(nowUs(),wave(0x31,1)),"second lane IQ magnitudes");
    auto invalid=wave(0x30,0,2); for(auto& p:invalid.dliaPoints) p.h1=0;
    require(recorder.recordFpgaHarmonics(nowUs(),invalid),"all invalid reference cycle admitted");
    auto partial=wave(0x30,0,3); partial.complete=false; partial.partial=true;
    require(recorder.recordFpgaHarmonics(nowUs(),partial),"incomplete cycle quality marker admitted");
    require(recorder.pause(),"ground pause drains");
    require(!recorder.recordFpgaHarmonics(nowUs(),wave()),"ground pause rejects data");
    auto spanningPause = wave(); spanningPause.firstHostTimeUs = Session::harmonicReceptionTimeUs();
    require(recorder.start(options),"ground resume same session");
    require(!recorder.recordFpgaHarmonics(nowUs(),spanningPause),"ground rejects cycles started before the resume boundary");
    require(recorder.recordFpgaHarmonics(nowUs(),wave(0x30,2,1,7)),"resumed cycle may reset id and change point count");
    auto adc=wave();adc.source=0x20;require(!recorder.recordFpgaHarmonics(nowUs(),adc),"ADC forwarding is not recorded");
    auto preview=wave();preview.preview=true;require(!recorder.recordFpgaHarmonics(nowUs(),preview),"preview cannot masquerade as complete recording");
    require(!recorder.stop().writeFailed,"ground stop seals files"); checkPackage(directory,5);
    require(!recorder.recordFpgaHarmonics(nowUs(),wave()),"sealed ground file rejects later callbacks");
    const auto config=QJsonDocument::fromJson(read(QDir(directory).filePath("config/device_config.json"))).object();
    require(config.contains("fpga") && config.contains("waveform_processing"),"device config captures FPGA settings and algorithm");
    const auto raw=read(QDir(directory).filePath("raw/waveform.dat")); const auto index=scan(directory);
    Session::HarmonicLayout first,last;
    Session::parseHarmonicLayout(QByteArrayView(raw).sliced(index.records.first().payloadOffset),index.records.first().header.payloadSize,first);
    Session::parseHarmonicLayout(QByteArrayView(raw).sliced(index.records.last().payloadOffset),index.records.last().header.payloadSize,last);
    require(last.segment>first.segment && last.cycle==first.cycle,"segment separates reused cycle ids across pause/resume");
    const auto metadata=Ground::SessionLoader::loadMetadata(directory); require(metadata.success,"common metadata loads");
    auto result=Ground::SessionWaveformRepository::loadCatalog(metadata.metadata);
    require(result.success && result.catalog.fpgaChoices.size()==6 && !result.catalog.fpgaSampleCache,"catalog indexes H1/H2 without precomputing full normalized history");
    require(result.catalog.fpgaComponent==6 && result.catalog.frameCount==4,"default normalized view has independent channel timeline");
    auto frame=Ground::SessionWaveformRepository::readFrame(result.catalog,0);
    require(frame.success && frame.samples.size()==4 && std::isnan(frame.samples[2]) && frame.samples[3]==10,"on-demand normalized frame");
    require(Ground::SessionWaveformRepository::readFrame(result.catalog,3).samples.size()==7,"per-frame length respected");
    const auto peaks=Ground::SessionWaveformRepository::loadCachedPeakSeries(result.catalog);
    require(peaks.success && peaks.peakValues.size()==4 && peaks.peakValues[0]==10 && std::isnan(peaks.peakValues[1]) && std::isnan(peaks.peakValues[2]),"viewer loads exact recorded per-cycle normalized peaks including quality gaps");
    auto cancelled=std::make_shared<std::atomic_bool>(true);
    require(Ground::SessionWaveformRepository::calculatePeakSeries(result.catalog,0,2,cancelled).cancelled,"on-demand calculations remain cancellable");
    const auto restricted=Ground::SessionWaveformRepository::calculatePeakSeries(result.catalog,0,1);
    require(restricted.success && restricted.peakValues[0]<2,"changed search range recalculates from H1/H2");
    for(int c=0;c<result.catalog.fpgaChoices.size();++c) {
        require(Ground::SessionWaveformRepository::selectFpgaWaveform(result.catalog,c),"select source and component");
        require(Ground::SessionWaveformRepository::readFrame(result.catalog,0).success,"all saved components readable");
    }
}
void skyRoundTripAndFailures() {
    QTemporaryDir temporary; SkySessionRecorder recorder; recorder.setFpgaConfiguration(FpgaControlConfig{});
    QString error; require(recorder.start(temporary.path(),"",0,&error),"Sky start"); const auto directory=recorder.sessionDirectory();
    require(recorder.recordFpgaHarmonics(nowUs(),wave()),"Sky admits cycle"); recorder.pause();
    require(!recorder.recordFpgaHarmonics(nowUs(),wave()),"Sky pause rejects cycle");
    auto spanningPause = wave(); spanningPause.firstHostTimeUs = Session::harmonicReceptionTimeUs();
    require(recorder.start(temporary.path(),"",0,&error),"Sky resumes");
    require(!recorder.recordFpgaHarmonics(nowUs(),spanningPause),"Sky rejects cycles started before resume");
    require(recorder.recordFpgaHarmonics(nowUs(),wave(0x31,1)),"Sky second lane");
    QElapsedTimer visibleDeadline;visibleDeadline.start();
    while(visibleDeadline.elapsed()<3000&&read(directory+"/sensors/waveform_features.csv").count('\n')<3)QThread::msleep(10);
    require(read(directory+"/sensors/waveform_features.csv").count('\n')==3,"Sky feature rows visible before recording stops");
    require(recorder.stop(&error),"Sky drains and seals"); checkPackage(directory,2);
    require(recorder.rawWaveformRecordCount()==2 && recorder.waveformFeatureRecordCount()==2 && recorder.rawFpgaRecordCount()==0,"status reports shared waveform and feature counts");
    for(bool flush:{false,true}) {
        auto storage=std::make_shared<FailingRecordingStorage>(); QTemporaryDir fault;
        SkySessionRecorder failing(storage); require(failing.start(fault.path(),"",0,&error),"Sky fault fixture start");
        storage->failWrites=!flush;storage->failFlush=flush;
        require(failing.recordFpgaHarmonics(nowUs(),wave()),"Sky write admission precedes asynchronous failure");
        require(!failing.stop(&error) && failing.storageFailed(),"Sky short write and flush failure mark incomplete");
        Ground::Session::GroundRecordingService ground(storage); Ground::Session::GroundRecordingOptions o; o.baseDirectory=fault.path();
        storage->failWrites=false;storage->failFlush=false;
        require(ground.start(o),"ground fault fixture start"); storage->failWrites=!flush;storage->failFlush=flush;
        require(ground.recordFpgaHarmonics(nowUs(),wave()),"ground write admission precedes asynchronous failure");
        require(ground.stop().writeFailed,"ground short write and flush failure mark incomplete");
    }
    QTemporaryDir budget; SkySessionRecorder bounded;require(bounded.start(budget.path(),"",0,&error),"queue budget fixture start");
    auto tooLarge=wave(0x30,0,1,2097152);
    require(!bounded.recordFpgaHarmonics(nowUs(),tooLarge) && bounded.storageFailed(),"bounded queue rejects oversized cycle explicitly");
    require(!bounded.stop(&error),"overflow cannot seal a successful session");
}
void recordingPrecedesPresentationCoalescing() {
    QTemporaryDir directory;
    Ground::Session::GroundRecordingService recorder;
    Ground::Session::GroundRecordingOptions options; options.baseDirectory=directory.path();
    require(recorder.start(options),"controller recording starts");
    Ground::Devices::FpgaDeviceController controller(Ground::Devices::makeFpgaUsbSimulator());
    auto *session=controller.findChild<Ground::Devices::FpgaDeviceSession*>();
    require(session,"controller has a waveform assembly session");
    int cycles=0;
    QObject::connect(&controller,&Ground::Devices::FpgaDeviceController::harmonicCycleReady,
        &controller,[&](quint64 host,const FpgaWave::CompletedStream& s) {
            require(s.hasFullCyclePeak && s.fullCyclePeak==10,"controller computes the full-cycle peak");
            require(recorder.recordFpgaHarmonics(host,s),"each completed cycle reaches recording admission"); ++cycles;
        },Qt::DirectConnection);
    // No event-loop delivery of presentation timers between these cycles.
    for(quint32 i=0;i<40;++i) session->waveformCompleted(wave(0x30,0,i));
    require(cycles==40,"recording observes every cycle before the UI can coalesce them");
    const auto path=recorder.status().sessionDirectory;
    require(!recorder.stop().writeFailed && scan(path).records.size()==40,"all cycles survive bounded asynchronous recording");
}
void concurrentLifecycle() {
    QTemporaryDir temporary; SkySessionRecorder recorder;
    std::atomic_bool running{true};std::atomic_uint attempts{0};const auto s=wave();
    std::thread producer([&] { while(running) {recorder.recordFpgaHarmonics(nowUs(),s);++attempts;QThread::msleep(1);} });
    bool valid=true; QString error;
    for(int i=0;i<8 && valid;++i) {
        valid=recorder.start(temporary.path(),"",0,&error); if(!valid)break;
        QThread::msleep(3);recorder.pause();valid=recorder.start(temporary.path(),"",0,&error);if(!valid)break;
        QThread::msleep(3);valid=recorder.stop(&error);if(!valid)break;
        const auto count=recorder.rawWaveformRecordCount();const auto index=scan(recorder.sessionDirectory());
        valid=index.success() && quint64(index.records.size())==count;QThread::msleep(2);
        valid=valid && recorder.rawWaveformRecordCount()==count;
    }
    running=false;producer.join();require(attempts>0 && valid,"concurrent callbacks and lifecycle preserve sealed sessions");
}
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv);
    commonFormatAndPreview(); groundRoundTrip(); recordingPrecedesPresentationCoalescing(); skyRoundTripAndFailures(); concurrentLifecycle();
    std::cout<<"Unified FPGA harmonic recording passed; physical USB not exercised\n";
}
