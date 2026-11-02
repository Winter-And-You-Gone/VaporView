#pragma once
#include "ground/calibration/CalibrationService.h"
#include "FpgaHarmonics.h"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <atomic>
#include <cstdlib>
#include <iostream>

namespace IndependentCalibrationTest
{
using namespace VaporView;
using namespace VaporView::Ground::Calibration;
inline void check(bool ok,const char *message) {if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
inline QByteArray read(const QString& path) {QFile f(path);check(f.open(QIODevice::ReadOnly),"read fixture");return f.readAll();}
inline void write(const QString& path,const QByteArray& bytes) {QFile f(path);check(f.open(QIODevice::WriteOnly),"write fixture");check(f.write(bytes)==bytes.size(),"write bytes");}
inline FpgaWave::CompletedStream cycle(int wave,int peak,quint32 id,bool invalid=false)
{
    FpgaWave::CompletedStream s;s.source=quint16(channelForWavelength(wave));s.schema=2;s.message=0x1001;s.format=0;
    s.complete=true;s.totalPoints=3;s.rate=10000;s.cycleId=id;s.timestamp=quint64(id)*10000000;
    for(int i=0;i<3;++i){FpgaWave::CompletedStream::DliaPoint p;p.hasHarmonics=true;p.h1=i?2:100;p.h2=i?peak*2:peak;s.dliaPoints.append(p);}
    if(invalid)s.overflow=true;s.normalizedFeature=FpgaWave::normalizedCycleFeature(s);return s;
}
struct Rig
{
    QTemporaryDir root;
    std::atomic<quint64> now{10000};
    std::shared_ptr<CalibrationService> service;
    RealtimePeakBus bus;
    quint32 cycleId=1;
    const QJsonObject identity{{"backend","simulator"},{"serial","SOFTWARE_TEST_ONLY"}};
    Rig() {
        check(root.isValid(),"isolated installation root");
        service=std::make_shared<CalibrationService>(root.path(),[this]{return now.load();});
        QObject::connect(&bus,&RealtimePeakBus::peakReady,service.get(),&CalibrationService::offer,Qt::DirectConnection);
        QObject::connect(&bus,&RealtimePeakBus::deviceChanged,service.get(),&CalibrationService::setDevice,Qt::DirectConnection);
        bus.setDevice(true,identity);service->synchronize();
    }
    void pair(int a,int b,bool invalid816=false) {
        const auto id=cycleId++;const auto host=1700000000000000ULL+now.load()*1000;
        bus.publish(host,cycle(816,a,id,invalid816));bus.publish(host,cycle(1368,b,id));service->synchronize();
    }
    void task() {service->createProject(QStringLiteral("软件测试双波长任务"),"ppmv");service->synchronize();}
    void stage(double concentration,int a,int b,quint64 duration=2000) {
        service->addStage(concentration,duration);service->synchronize();pair(a,b);
        const int i=int(service->view().project.stages.size())-1;service->startStage(i);service->synchronize();
        check(service->view().acquiring,"independent stage starts without Session");
        for(quint64 elapsed=0;elapsed<duration;elapsed+=500){now+=500;pair(a,b);}
        check(service->view().project.stages[i].status=="complete","configured effective duration completes stage");
    }
    CalibrationFile complete() {
        task();stage(100,2,4);stage(200,4,8);stage(300,6,12);
        service->fitProject();service->synchronize();check(service->view().project.validate(true).isEmpty(),"both fits valid");
        service->generateFile(QStringLiteral("中文 软件测试定标"));service->synchronize();
        const auto files=service->view().files;check(files.size()==1,"self-contained file generated");
        QString error;auto f=CalibrationFileStore(root.path()).read(files[0],false,&error);check(f.valid,qPrintable(error));return f;
    }
};
}
