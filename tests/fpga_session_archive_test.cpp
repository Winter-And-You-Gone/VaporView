#include "ground/session/GroundRecordingService.h"
#include "shared/session/FpgaSessionArchive.h"
#include "shared/session/SessionManifest.h"
#include "ground/session/SessionWaveformRepository.h"
#include "LegacyFpgaArchiveFixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cstdlib>
#include <iostream>
#include <cmath>

using namespace VaporView::Session;
using namespace VaporView::Ground::Session;
namespace {
void check(bool ok,const char *message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
QByteArray frame(quint32 sequence,quint16 source=0x40,quint16 message=0x1100){
    const auto bytes=VaporView::FpgaVlp1::buildFrame(VaporView::FpgaVlp1::FrameType::Data,
        sequence,source,message,{1,0,0,0},true,0x83,0xfedcba9876543210ULL,9);
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
QByteArray wave(quint16 source,quint32 cycle,quint16 fragment=0,quint32 flags=3)
{
    QByteArray payload;
    auto put16=[&](quint16 v){payload.append(char(v));payload.append(char(v>>8));};
    auto put32=[&](quint32 v){for(int i=0;i<4;++i)payload.append(char(v>>(i*8)));};
    if(source==0x30) {
        put16(2);put16(0);put32(10000);put32(4);put16(fragment);put16(2);
        put32(fragment*2);put32(2);put32(8);put32(0);
        put32(1);put32(10+fragment*20);put32(2);put32(20+fragment*20);
    } else {
        put16(1);put16(24);put32(1000000);put32(2);put32(0);put32(0);
        put32(quint32(-5));put32(12);
    }
    const auto bytes=VaporView::FpgaVlp1::buildFrame(VaporView::FpgaVlp1::FrameType::Data,
        cycle*2+fragment,source,source==0x30?0x1001:0x1000,
        std::vector<std::uint8_t>(payload.begin(),payload.end()),true,flags,cycle*100,cycle);
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
void iqWaveformChoices()
{
    using namespace VaporView;
    using namespace VaporView::Ground;
    QTemporaryDir temp;LegacyFpgaArchiveFixture recorder;GroundRecordingOptions options;options.baseDirectory=temp.path();
    check(recorder.start(options),"IQ choices recording starts");const auto directory=recorder.status().sessionDirectory;
    for(quint16 source:{0x30,0x31}) {
        QByteArray payload;
        const auto half=[&](quint16 v){payload.append(char(v));payload.append(char(v>>8));};
        const auto word=[&](quint32 v){for(int i=0;i<4;++i)payload.append(char(v>>(i*8)));};
        half(2);half(2);word(10000);word(1);half(0);half(1);word(0);word(1);word(24);word(0);
        for(quint32 value:{3,4,5,12,5,13})word(value+(source==0x31?100:0));
        const auto bytes=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,source,source,0x1001,std::vector<std::uint8_t>(payload.begin(),payload.end()),true,3,100,1);
        check(recorder.recordFpgaFrame(1000+source,QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size())),"IQ waveform records");
    }
    check(!recorder.stop().writeFailed,"IQ choices recording drains");
    const auto metadata=SessionLoader::loadMetadata(directory);auto result=SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(result.success && result.catalog.fpgaChoices.size()==14,"each DLIA lane exposes normalized H2 plus raw H1/H2 and all four IQ components");
    check(result.catalog.fpgaComponent==6,"FPGA replay selects normalized H2 by default");
    const float expected[]{13,5,3,4,5,12};
    for(int i=0;i<result.catalog.fpgaChoices.size();++i) {
        check(SessionWaveformRepository::selectFpgaWaveform(result.catalog,i),"IQ choice selects");
        const auto frame=SessionWaveformRepository::readFrame(result.catalog,0);
        const float offset=result.catalog.fpgaSource==0x31?100:0;
        const float value=result.catalog.fpgaComponent==6 ? (13+offset)/(5+offset) : expected[result.catalog.fpgaComponent]+offset;
        check(frame.success && frame.samples==QVector<float>{value},"normalized replay and raw diagnostic components preserve independent lane values");
    }
    auto retained = result.catalog;
    const QString cachePath = retained.fpgaSampleCache->path();
    result = {};
    check(QFileInfo::exists(cachePath) && SessionWaveformRepository::readFrame(retained, 0).success,
          "catalog copies retain their indexed samples for background readers");
    retained = {};
    check(!QFileInfo::exists(cachePath), "last catalog release removes the temporary sample cache");
}
void normalizedReferenceGaps()
{
    using namespace VaporView;
    using namespace VaporView::Ground;
    for(quint16 format:{0,1}) {
        QTemporaryDir temp;LegacyFpgaArchiveFixture recorder;GroundRecordingOptions options;options.baseDirectory=temp.path();
        check(recorder.start(options),"normalization reference recording starts");const auto directory=recorder.status().sessionDirectory;
        for(quint32 cycle=0;cycle<2;++cycle) {
            QByteArray payload;
            const auto half=[&](quint16 v){payload.append(char(v));payload.append(char(v>>8));};
            const auto word=[&](quint32 v){for(int i=0;i<4;++i)payload.append(char(v>>(i*8)));};
            half(2);half(format);word(10000);word(3);half(0);half(1);word(0);word(3);word(format?16:8);word(0);
            for(int point=0;point<3;++point) {
                word(cycle==0&&point==1?4:0);
                if(format)word(0);
                word(point==1?2:99999);
                if(format)word(0);
            }
            const auto bytes=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,cycle,0x30,0x1001,
                std::vector<std::uint8_t>(payload.begin(),payload.end()),true,3,100+cycle,cycle);
            check(recorder.recordFpgaFrame(1000+cycle,QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size())),"normalization reference frame records");
        }
        check(!recorder.stop().writeFailed,"normalization reference recording drains");
        const auto metadata=SessionLoader::loadMetadata(directory);
        const auto catalog=SessionWaveformRepository::loadCatalog(metadata.metadata);
        check(catalog.success&&catalog.catalog.fpgaComponent==6,"replay defaults to normalized H2 for harmonic and IQ formats");
        const auto frame=SessionWaveformRepository::readFrame(catalog.catalog,0);
        check(frame.success&&frame.samples.size()==3&&std::isnan(frame.samples[0])&&frame.samples[1]==0.5&&std::isnan(frame.samples[2]),
            "replay normalization preserves invalid references as gaps at the original sample indices");
        const auto peaks=SessionWaveformRepository::calculatePeakSeries(catalog.catalog,0,-1);
        check(peaks.success&&peaks.peakValues.size()==2&&peaks.peakValues[0]==0.5&&std::isnan(peaks.peakValues[1]),
            "replay peaks ignore invalid references and leave all-invalid cycles empty");
    }
}
void waveformRecording()
{
    using namespace VaporView::Ground;
    QTemporaryDir temp;
    LegacyFpgaArchiveFixture recorder;GroundRecordingOptions options;options.baseDirectory=temp.path();
    check(recorder.start(options),"wave session start");
    const QString directory=recorder.status().sessionDirectory;
    quint64 host=1000;
    auto record=[&](const QByteArray& bytes) {
        check(recorder.recordFpgaFrame(++host,bytes),"decoded wave queued");
        check(recorder.recordFpgaUsbBytes(++host,bytes.left(17)),"USB split queued");
        check(recorder.recordFpgaUsbBytes(++host,bytes.mid(17)),"USB tail queued");
    };
    record(frame(1));record(frame(2,0x42,0x1200));record(wave(0x20,1));record(wave(0x30,1,1));record(wave(0x30,1,0));
    record(wave(0x30,2,0));
    check(recorder.pause(),"wave pause drains");
    check(recorder.start(options),"wave resume");
    record(wave(0x30,2,1)); // Cannot stitch a scan across the pause boundary.
    record(wave(0x30,3,0,0x28));record(wave(0x30,3,1,0x28)); // Partial/overflow.
    record(wave(0x30,4,0));record(wave(0x30,4,1));
    check(!recorder.stop().writeFailed,"wave stop");
    const auto status=recorder.status();
    check(status.waveformFrames==2,"counts only two complete DLIA cycles, not fragments or duplicate USB/IN");
    const auto counts=status.fpgaStatistics.value("data_records").toObject();
    check(counts.value("64").toString()=="1" && counts.value("66").toString()=="1" && counts.value("waveform").toString()=="9","per-device validated data counts");
    auto metadata=SessionLoader::loadMetadata(directory);
    check(metadata.success && metadata.metadata.waveformFrames==2,"new manifest waveform count");
    metadata.metadata.waveformFrames=0; // Older recordings had the same data but a zero manifest count.
    const auto catalog=SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(catalog.success && catalog.catalog.frameCount==2 && catalog.catalog.sourceFileCount()==1,"old FPGA archive is a waveform source");
    check(catalog.catalog.fpgaSource==0x30 && catalog.catalog.pointsPerFrame==4,"DLIA channel selected without mixing RAW");
    for(quint64 i=0;i<2;++i) {
        const auto loaded=SessionWaveformRepository::readFrame(catalog.catalog,i);
        check(loaded.success && loaded.error.isEmpty() && loaded.samples==QVector<float>({10,10,30,20}),"fragmented H2/H1 replay normalizes matching points");
        check(loaded.timestampUs==catalog.catalog.fpgaTimestampsUs.at(i),"wave host timestamp retained");
    }
    auto selection=catalog.catalog;check(selection.fpgaChoices.size()==4,"normalized H2, RAW and both raw harmonic components are selectable");
    for(int choice=0;choice<selection.fpgaChoices.size();++choice) {
        check(SessionWaveformRepository::selectFpgaWaveform(selection,choice),"select FPGA component");const auto loaded=SessionWaveformRepository::readFrame(selection,0);
        check(loaded.success,"selected component frame readable");
        if(selection.fpgaSource==0x20)check(loaded.samples==QVector<float>({-5,12}),"RAW choice keeps signed values");
        else if(selection.fpgaComponent==1)check(loaded.samples==QVector<float>({1,2,1,2}),"H1 selection reads H1 words independently");
        else if(selection.fpgaComponent==0)check(loaded.samples==QVector<float>({10,20,30,40}),"raw H2 remains available without normalization");
    }
    const auto peaks=SessionWaveformRepository::calculatePeakSeries(catalog.catalog,0,-1);
    check(peaks.success && peaks.peakValues==QVector<float>({30,30}),"FPGA peak series uses normalized H2 rather than raw H2");
    const auto cancel=std::make_shared<std::atomic_bool>(true);
    check(SessionWaveformRepository::calculatePeakSeries(catalog.catalog,0,-1,cancel).cancelled,"FPGA peak calculation cancellation");

    const QString archivePath = FpgaSessionArchive::rawPath(directory);
    check(QFile::rename(archivePath, archivePath + QStringLiteral(".held")), "temporarily move indexed source");
    for (int choice = selection.fpgaChoices.size() - 1; choice >= 0; --choice) {
        check(SessionWaveformRepository::selectFpgaWaveform(selection, choice), "indexed component selects");
        for (quint64 index = selection.frameCount; index-- > 0;) {
            const auto loaded = SessionWaveformRepository::readFrame(selection, index);
            const QVector<float> expected = selection.fpgaSource == 0x20 ? QVector<float>{-5, 12}
                : selection.fpgaComponent == 1 ? QVector<float>{1, 2, 1, 2}
                : selection.fpgaComponent == 6 ? QVector<float>{10, 10, 30, 20} : QVector<float>{10, 20, 30, 40};
            check(loaded.success && loaded.samples == expected && loaded.sourceFilename == archivePath &&
                      loaded.timestampUs == selection.fpgaTimestampsUs.at(index),
                  "random indexed reads preserve full samples, timestamps and source without replaying the archive");
        }
    }
    check(SessionWaveformRepository::calculatePeakSeries(catalog.catalog, 0, -1).peakValues == peaks.peakValues,
          "peak calculation also uses indexed samples without replaying the archive");
    check(QFile::rename(archivePath + QStringLiteral(".held"), archivePath), "restore indexed source");

    // Frame-only archives and RAW-only capture remain valid fallback sources.
    check(recorder.start(options),"RAW-only start");
    check(recorder.recordFpgaFrame(++host,wave(0x20,9)),"RAW-only record");
    check(!recorder.stop().writeFailed,"RAW-only stop");
    metadata=SessionLoader::loadMetadata(recorder.status().sessionDirectory);
    const auto rawCatalog=SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(rawCatalog.success && rawCatalog.catalog.fpgaSource==0x20 && rawCatalog.catalog.frameCount==1,"RAW fallback");
    check(SessionWaveformRepository::readFrame(rawCatalog.catalog,0).samples==QVector<float>({-5,12}),"signed RAW samples");
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    waveformRecording();iqWaveformChoices();normalizedReferenceGaps();
    QTemporaryDir temp; check(temp.isValid(),"temp directory");
    GroundRecordingOptions options; options.baseDirectory=temp.path();
    LegacyFpgaArchiveFixture recorder;
    check(recorder.start(options),"start");
    const QString directory=recorder.status().sessionDirectory;
    const QByteArray wire=frame(7);
    const auto unpaddedVector=VaporView::FpgaVlp1::buildFrame(VaporView::FpgaVlp1::FrameType::Data,
        1,0x40,0x1100,{1},false);
    const QByteArray unpadded(reinterpret_cast<const char*>(unpaddedVector.data()),unpaddedVector.size());
    check(FpgaSessionArchive::describe(99,FpgaArchiveKind::Frame,unpadded).value(QStringLiteral("crc_valid")).toBool(),"codec Frame.bytes without padding supported");
    check(recorder.recordFpgaFrame(100,wire),"frame queued");
    check(recorder.recordFpgaUsbBytes(101,wire.left(13)),"partial USB queued");
    check(recorder.recordFpgaUsbBytes(102,wire.mid(13)),"rest USB queued");
    check(recorder.recordFpgaCommand(103,wire),"command queued");
    check(recorder.pause(),"pause drains worker");
    check(!recorder.recordFpgaFrame(104,wire),"pause rejects frames");
    check(recorder.start(options),"resume same session");
    check(recorder.recordFpgaUsbBytes(105,wire.left(10)),"partial before reconnect");
    check(recorder.recordFpgaSnapshot(106,{{QStringLiteral("event"),QStringLiteral("reconnect")}}),"reconnect marker");
    check(recorder.recordFpgaUsbBytes(107,wire.mid(10)),"orphan tail preserved");
    QByteArray corrupt=wire;corrupt[40]^=1;
    check(recorder.recordFpgaUsbBytes(108,corrupt+frame(8)),"CRC damage preserved");
    const auto stop=recorder.stop();check(!stop.writeFailed,"stop closes files");
    check(!recorder.recordFpgaFrame(109,wire),"closed rejects frames");
    const auto index=FpgaSessionArchive::scan(directory);check(index.success(),"index scan");
    check(index.records.size()==10,"frames USB markers counted once");
    int replayed=0;
    check(FpgaSessionArchive::replay(directory,[&](quint64 timestamp,const auto& f){
        check(f.header.timestamp==0xfedcba9876543210ULL,"uint64 FPGA tick retained");
        check(f.header.flags==0x83 && f.header.cycleId==9 && f.header.source==0x40,"VLP metadata retained");
        check((replayed==0 && f.header.sequence==7 && timestamp==102) ||
              (replayed==1 && f.header.sequence==8 && timestamp==108),"USB replay skips duplicate IN, reconnect tail and CRC damage");
        ++replayed;return true;
    }),"replay");check(replayed==2,"two valid frames");
    check(!FpgaSessionArchive::describe(0,FpgaArchiveKind::Frame,corrupt).value(QStringLiteral("crc_valid")).toBool(),"CRC report");
    for(const auto format:{FpgaSessionArchive::ExportFormat::Csv,FpgaSessionArchive::ExportFormat::Json,FpgaSessionArchive::ExportFormat::Bin}){
        const QString filename=QDir(temp.path()).filePath(QString::number(int(format))+QStringLiteral(".export"));
        check(FpgaSessionArchive::exportTo(directory,filename,format),"export");
        QFile file(filename);check(file.open(QIODevice::ReadOnly),"read export");const auto bytes=file.readAll();
        if(format==FpgaSessionArchive::ExportFormat::Bin)check(bytes==wire+wire+corrupt+frame(8),"BIN exact USB includes corrupt data and reconnect tail");
        if(format==FpgaSessionArchive::ExportFormat::Json)check(QJsonDocument::fromJson(bytes).isArray(),"valid JSON array");
        if(format==FpgaSessionArchive::ExportFormat::Csv)check(bytes.contains("fpga_tick"),"CSV schema");
    }
    // Calibrate the public scan callback against this fixture so cancellation
    // below occurs after real output rows, without timer/thread races.
    int scanChecks=0;
    VaporView::SessionRawDat::RawScanOptions scanOptions;
    scanOptions.isCancelled=[&]{++scanChecks;return false;};
    check(FpgaSessionArchive::scan(directory,scanOptions).success() && scanChecks>0,"scan checks cancellation");
    const QByteArray previousOutput("existing export must survive cancellation\n");
    for(const auto format:{FpgaSessionArchive::ExportFormat::Csv,FpgaSessionArchive::ExportFormat::Json,FpgaSessionArchive::ExportFormat::Bin}){
        for(bool cancelWhileWriting:{false,true}){
            const QString filename=QDir(temp.path()).filePath(QStringLiteral("cancel-%1-%2.export").arg(int(format)).arg(cancelWhileWriting));
            QFile original(filename);
            check(original.open(QIODevice::WriteOnly) && original.write(previousOutput)==previousOutput.size(),"seed existing output");
            original.close();
            const QStringList before=QDir(temp.path()).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot);
            int checks=0;
            QString cancelError;
            const bool exported=FpgaSessionArchive::exportTo(directory,filename,format,&cancelError,[&]{
                ++checks;
                // Every format has written something by the third visitor:
                // CSV/JSON rows, or the first USB chunk in BIN.
                return !cancelWhileWriting || checks>=scanChecks+3;
            });
            check(!exported && cancelError==QStringLiteral("FPGA export cancelled"),"export reports explicit cancellation");
            check(cancelWhileWriting?checks>scanChecks:checks==1,"cancel during requested scan/write stage");
            QFile preserved(filename);check(preserved.open(QIODevice::ReadOnly),"cancelled output still exists");
            check(preserved.readAll()==previousOutput,"cancel never replaces existing output");preserved.close();
            check(QDir(temp.path()).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot)==before,"cancel removes temporary output");
        }
    }
    check(!FpgaSessionArchive::exportTo(directory,FpgaSessionArchive::rawPath(directory),FpgaSessionArchive::ExportFormat::Bin),"cannot overwrite archive");
    QFile raw(FpgaSessionArchive::rawPath(directory));check(raw.open(QIODevice::WriteOnly|QIODevice::Append),"append interrupted tail");raw.write("VWRD",4);raw.close();
    check(FpgaSessionArchive::scan(directory).recovered(),"interrupted tail recovery");
    const auto legacy=sessionManifestToJson(SessionManifest{});
    check(!legacy.value(QStringLiteral("raw_files")).toObject().contains(QStringLiteral("fpga_vlp1")),"legacy manifest stays unchanged");
    QFile manifest(QDir(directory).filePath(QStringLiteral("session.json")));check(manifest.open(QIODevice::ReadOnly),"manifest read");
    const auto parsed=sessionManifestFromJson(QJsonDocument::fromJson(manifest.readAll()).object());
    check(parsed.success && parsed.manifest.fpgaRecords==10,"FPGA manifest count roundtrip");
    check(recorder.status().rawFpgaRecords==10,"recording status includes archived FPGA records after close");
    std::cout<<"FPGA archive software tests passed; USB hardware not exercised\n";
    return 0;
}
