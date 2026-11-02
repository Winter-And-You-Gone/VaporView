#include "IndependentCalibrationFixture.h"
#include "ground/session/GroundRecordingService.h"
#include "FpgaTelemetry.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <cmath>

using namespace IndependentCalibrationTest;
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    {
        Rig r;r.task();r.service->addStage(100);r.service->synchronize();
        check(r.service->view().project.stages[0].configuredMs==600000,"default stage is ten minutes");
        r.service->startStage(0);r.service->synchronize();check(!r.service->view().acquiring,"missing dual stream rejected");
        r.pair(2,4);r.service->startStage(0);r.service->synchronize();r.now+=1000;r.pair(2,4);
        auto v=r.service->view();check(v.project.stages[0].effectiveMs==1000,"common two-channel effective time");
        const auto row=v.project.stages[0].samples[816][0];
        r.service->offer(row);r.service->synchronize();check(r.service->view().project.stages[0].samples[816].size()==1,"duplicate record not admitted twice");
        r.now+=5000;r.service->synchronize();v=r.service->view();check(!v.acquiring&&v.project.stages[0].effectiveMs==1000,"missing time never credited");
        r.now+=7000;r.service->synchronize();
        check(r.service->view().project.stages[0].wallMs==13000,"wall elapsed includes paused interruption without effective credit");
        r.service->pauseStage(true);r.service->synchronize();
        check(r.service->view().project.stages[0].status=="incomplete","stop after automatic pause preserves incomplete status");
        r.service->editStage(0,999,1,{});r.service->synchronize();check(r.service->view().project.stages[0].concentration==100,"started stage immutable");
        r.bus.setDevice(false,{});r.service->synchronize();check(!r.service->view().connected,"disconnect event reaches acquisition");
        check(!QFileInfo::exists(QDir(r.root.path()).filePath("data")),"no ordinary Session directory");
        const auto draft=r.service->view().drafts;
        CalibrationFileStore store(r.root.path());QString e;const auto p=store.read(store.drafts().first(),true,&e);
        check(p.valid&&p.project.stages[0].effectiveMs==1000,"interrupted independent draft survives");
    }
    Rig r;const auto f=r.complete();auto p=f.project;
    check(p.origin=="software_test","simulated acquisition explicitly marked");
    check(std::abs(p.fits[816].parameters.a-50)<1e-10&&std::abs(p.fits[1368].parameters.a-25)<1e-10,"independent channel slopes");
    check(p.fits[816].parameters.sampleCount==3&&p.fits[816].parameters.rmse==0,"stage means have equal weight");
    check(p.stages[0].samples[816].size()==4&&p.stages[0].samples[1368].size()==4,"same collection window");
    const auto id=p.stages[0].samples[816][0].feature.id;QString error;
    check(p.exclude(p.stages[0].id,816,{id},true,QStringLiteral("软件测试人工剔除"),&error),"single-point exclusion");
    check(p.fits.isEmpty()&&p.stages[0].statistics(816).used==3&&p.stages[0].statistics(1368).used==4,"review invalidates fits and is wavelength independent");
    check(p.exclude(p.stages[0].id,816,{id},false,QStringLiteral("软件测试恢复"),&error),"restore audit");
    check(p.stages[0].samples[816].size()==4&&p.stages[0].operations.size()==2,"raw samples and full audit retained");
    p.fit();check(p.validate(true).isEmpty(),"refit after review");
    {
        auto audited=f.project;auto& stage=audited.stages[0];
        for(int i=0;i<4;++i)stage.samples[816][i].feature.peak=i+1;
        const auto rawBefore=stage.toJson()["channels"];
        const QSet<QString> batch{stage.samples[816][0].feature.id,stage.samples[816][1].feature.id};
        check(audited.exclude(stage.id,816,batch,true,QStringLiteral("软件测试批量审核"),&error),"batch exclusion");
        const auto statistics=stage.statistics(816);
        check(statistics.used==2 && statistics.mean==3.5 && std::abs(statistics.standardDeviation-std::sqrt(0.5))<1e-12,"review recomputes stage mean and sample deviation");
        check(stage.statistics(1368).used==4 && stage.toJson()["channels"]==rawBefore,"batch review preserves both raw channels");
        audited.fit();QTemporaryDir auditRoot;CalibrationFileStore auditStore(auditRoot.path());CalibrationFile saved;
        check(auditStore.generate(audited,QStringLiteral("软件审核往返"),saved,&error),"review audit persists in self-contained file");
        auto restored=auditStore.read(saved.filename,false,&error).project;
        check(restored.stages[0].operations.size()==2 && restored.stages[0].statistics(816).used==2,"batch audit reload retains exclusions");
        check(restored.exclude(stage.id,816,batch,false,QStringLiteral("软件测试批量恢复"),&error) && restored.fits.isEmpty(),"batch restoration invalidates saved fit revision");
        check(restored.stages[0].statistics(816).used==4 && restored.stages[0].operations.size()==4 && restored.stages[0].toJson()["channels"]==rawBefore,"batch restoration retains complete raw and audit history");
    }
    auto invalid=p.stages[0].samples[816][0];invalid.feature.qualityFlags|=8;
    p.stages[0].samples[816][0]=invalid;
    check(p.stages[0].statistics(816).invalid==1,"manual restoration never overrides hardware quality");
    CalibrationFileStore store(r.root.path());CalibrationFile duplicate;
    check(store.generate(f.project,QStringLiteral("中文 软件测试定标"),duplicate,&error)&&duplicate.filename!=f.filename,"Chinese filename collision creates another file");
    auto bytes=read(f.filename);bytes[bytes.size()/2]^=1;
    check(!CalibrationFileStore::decode(bytes,false,&error).valid,"corrupt SHA rejected");
    auto bad=f.document;bad["schema_version"]=99;bad.remove("sha256");bad["sha256"]=QString::fromLatin1(calibrationDigest(bad));
    check(!CalibrationFileStore::decode(QJsonDocument(bad).toJson(),false,&error).valid,"unsupported schema rejected even with correct digest");
    auto snapshot=f.project.toJson();auto mapping=snapshot["hardware_mapping"].toObject();mapping["816"]=0x30;snapshot["hardware_mapping"]=mapping;
    CalibrationProject::fromJson(snapshot,&error);check(!error.isEmpty(),"incompatible mapping rejected");
    auto wav=cycle(816,7,55);const auto encoded=FpgaRemote::encodeCycleFeature(1700000000000000ULL,wav);
    FpgaWave::CompletedStream decoded;quint64 host=0;check(FpgaRemote::parseCycleFeature(encoded,host,decoded)&&decoded.normalizedFeature->peak==7,"remote full-cycle summary round trip");
    auto payload=VaporView::Session::encodeHarmonics(wav,1);
    VaporView::Session::HarmonicLayout layout;check(VaporView::Session::parseHarmonicLayout(payload,payload.size(),layout),"raw harmonic layout");
    check(VaporView::Session::harmonicFeatureCsv(host,1,payload)==VaporView::Session::harmonicFeatureCsv(host,1,layout,*wav.normalizedFeature),"Session and realtime peak CSV exactly equal");
    {
        RealtimePeakBus remote;remote.setDevice(true,{{"transport","remote_fpga"}});QVector<PeakRecord> received;
        QObject::connect(&remote,&RealtimePeakBus::peakReady,&remote,[&](const PeakRecord& row){received.append(row);});
        auto full=cycle(816,2,100);full.featureStreamId=calibrationUuid();full.featureSequence=1;remote.publish(host,full,"remote_fpga");
        full.cycleId=101;full.featureSequence=3;remote.publish(host+1000,full,"remote_fpga");
        check(received.size()==2 && received[0].feature.invalidReason().isEmpty() && !received[1].feature.invalidReason().isEmpty(),"remote sequence loss becomes explicit continuity quality failure");
    }
    int events=0;QObject subscriber;
    auto connection=QObject::connect(&r.bus,&RealtimePeakBus::peakReady,&subscriber,[&](const PeakRecord&){++events;});
    r.bus.publish(host,wav);r.bus.publish(host,wav);check(events==1,"same cycle dispatched once");QObject::disconnect(connection);
    r.bus.publish(host,cycle(816,7,56));check(events==1,"subscriber disconnected safely");
    {
        auto destroyed=std::make_unique<QObject>();
        QObject::connect(&r.bus,&RealtimePeakBus::peakReady,destroyed.get(),[&](const PeakRecord&){++events;});
        destroyed.reset();r.bus.publish(host,cycle(816,7,57));check(events==1,"subscriber destruction automatically disconnects");
    }
    {
        Rig recovery;recovery.task();recovery.service->addStage(100,4000);recovery.service->synchronize();recovery.pair(2,4);
        recovery.service->startStage(0);recovery.service->synchronize();recovery.now+=1000;recovery.pair(2,4);
        const auto savedRow=recovery.service->view().project.stages[0].samples[816][0];
        recovery.service->pauseStage();recovery.service->synchronize();const auto draft=CalibrationFileStore(recovery.root.path()).drafts().first();
        recovery.service.reset();recovery.now+=50000;
        recovery.service=std::make_shared<CalibrationService>(recovery.root.path(),[&]{return recovery.now.load();});
        QObject::connect(&recovery.bus,&RealtimePeakBus::peakReady,recovery.service.get(),&CalibrationService::offer,Qt::DirectConnection);
        recovery.service->setDevice(true,recovery.identity);recovery.service->openProject(draft,true);recovery.service->synchronize();
        check(recovery.service->view().project.stages[0].effectiveMs==1000,"restart never credits offline gap");
        recovery.pair(2,4);recovery.service->startStage(0);recovery.service->synchronize();
        recovery.service->offer(savedRow);recovery.service->synchronize();
        check(recovery.service->view().project.stages[0].samples[816].size()==1,"restored draft rejects previously saved IDs");
        for(int i=0;i<6;++i){recovery.now+=500;recovery.pair(2,4);}
        check(recovery.service->view().project.stages[0].status=="complete","restart resumes remaining effective duration");
    }
    {
        Rig repeated;repeated.task();repeated.stage(100,2,4);repeated.stage(200,5,10,4000);repeated.stage(300,6,12);repeated.stage(100,3,6);
        repeated.service->fitProject();repeated.service->synchronize();const auto result=repeated.service->view().project;
        const auto equal=fitLinearPoints({{2,100},{5,200},{6,300},{3,100}});
        check(result.fits[816].parameters.a==equal.fit.a && result.fits[816].parameters.b==equal.fit.b && result.fits[816].parameters.sampleCount==4,"repeat stages and unequal cycle counts remain equal-weight fit inputs");
        auto two=result;two.stages=two.stages.mid(0,2);two.invalidateFits();two.fit();
        check(!two.fits[816].warning.isEmpty(),"two-level fit has quality warning");
        two.stages[1].concentration=two.stages[0].concentration;two.invalidateFits();
        check(!two.fit().isEmpty(),"one concentration level fails fit");
    }
    {
        Rig quality;quality.task();quality.service->addStage(100,4000);quality.service->synchronize();quality.pair(2,4);quality.service->startStage(0);quality.service->synchronize();
        quality.now+=500;quality.pair(2,4,true);
        const auto state=quality.service->view();check(!state.acquiring && !state.project.stages[0].effectiveMs && state.project.stages[0].statistics(816).invalid==1,"invalid cycle retained and pauses without crediting bad interval");
        quality.service->startStage(0);quality.service->synchronize();check(!quality.service->view().acquiring,"invalid latest cycle cannot reuse a preceding good preflight");
        quality.service->setExternalSessionOpen(true);quality.pair(2,4);quality.service->startStage(0);quality.service->synchronize();
        check(!quality.service->view().acquiring,"externally started or paused remote Session blocks collection");
    }
    {
        QTemporaryDir blocked;write(QDir(blocked.path()).filePath("calibrations"),"not a directory");
        CalibrationFileStore denied(blocked.path());CalibrationFile output;
        check(!denied.generate(f.project,"blocked",output,&error) && error.contains(QStringLiteral("安装目录")),"unwritable target returns actionable error without another-directory fallback");
        check(!QFileInfo::exists(QDir(blocked.path()).filePath("data")),"write failure never falls back to Session data");
    }
    std::cout<<"Independent calibration software tests passed; no hardware accuracy claim.\n";
}
