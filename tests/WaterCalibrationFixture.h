#pragma once
#include "ground/calibration/WaterCalibration.h"
#include "ground/session/GroundRecordingService.h"
#include "shared/session/HarmonicWaveform.h"
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <cstdlib>
#include <iostream>

namespace WaterCalibrationTest
{
inline void check(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
inline QByteArray read(const QString& filename)
{
    QFile file(filename); check(file.open(QIODevice::ReadOnly),"read fixture"); return file.readAll();
}
inline QString recordSession(const QString& base, int peak, bool faults = false, int infraredPeak = 0)
{
    using namespace VaporView;
    Ground::Session::GroundRecordingService recorder;
    Ground::Session::GroundRecordingOptions options; options.baseDirectory=base;
    options.fpgaConfiguration={{"backend","simulator"}};
    check(recorder.start(options),"start software-test Session"); const auto directory=recorder.status().sessionDirectory;
    const auto initialFeatures=Ground::SessionWaveformFeatures::read(directory);
    check(initialFeatures.success&&initialFeatures.records.isEmpty(),"empty active Session waits for its first cycle without a parse error");
    for (quint16 channel : {0x30,0x31}) for(int cycle=0;cycle<(faults?4:1);++cycle) {
        const int channelPeak=channel==0x30&&infraredPeak?infraredPeak:peak;
        FpgaWave::CompletedStream wave; wave.source=channel;wave.message=0x1001;wave.schema=2;wave.format=0;
        wave.cycleId=quint32(cycle);wave.rate=10000;wave.totalPoints=3;wave.bytesPerPoint=8;
        wave.complete=true;wave.timestamp=0xfedcba9876543210ULL;wave.flags=3;
        for(int i=0;i<3;++i){FpgaWave::CompletedStream::DliaPoint p;p.hasHarmonics=true;
            p.h1=i==0?100:2;p.h2=i==0?channelPeak:channelPeak*2;wave.dliaPoints.append(p);}
        if(cycle==1)wave.dliaPoints[0].h1=0; // still a valid maximum; invalid point excluded by existing algorithm.
        if(cycle==2)for(auto& p:wave.dliaPoints)p.h1=0;
        if(cycle==3)wave.overflow=true;
        check(recorder.recordFpgaHarmonics(1700000000000000ULL+quint64(cycle)*1000+channel,wave),"record existing harmonic feature chain");
    }
    QElapsedTimer wait;wait.start();bool visible=false;
    while(wait.elapsed()<3000){
        const auto features=Ground::SessionWaveformFeatures::read(directory);
        if(features.success&&features.records.size()==(faults?8:2)){visible=true;break;}
        QThread::msleep(10);
    }
    check(visible,"complete features visible to live reader before recorder stops");
    check(!recorder.stop().writeFailed,"drain Session");return directory;
}
}
