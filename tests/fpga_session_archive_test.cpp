#include "ground/session/GroundRecordingService.h"
#include "shared/session/FpgaSessionArchive.h"
#include "shared/session/SessionManifest.h"
#include "FailingRecordingStorage.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <cstdlib>
#include <iostream>

using namespace VaporView::Session;
using namespace VaporView::Ground::Session;
namespace {
void check(bool ok,const char *message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
QByteArray frame(quint32 sequence){
    const auto bytes=VaporView::FpgaVlp1::buildFrame(VaporView::FpgaVlp1::FrameType::Data,
        sequence,0x40,0x1100,{1,0,0,0},true,0x83,0xfedcba9876543210ULL,9);
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir temp; check(temp.isValid(),"temp directory");
    GroundRecordingOptions options; options.baseDirectory=temp.path();
    GroundRecordingService recorder;
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
    for(bool flushFailure:{false,true}){
        QTemporaryDir faultTemp;GroundRecordingOptions faultOptions;faultOptions.baseDirectory=faultTemp.path();
        auto storage=std::make_shared<FailingRecordingStorage>();GroundRecordingService failed(storage);
        check(failed.start(faultOptions),"fault session start");
        storage->failWrites.store(!flushFailure);storage->failFlush.store(flushFailure);
        check(failed.recordFpgaFrame(200,wire),"fault frame accepted before write");
        check(failed.stop().writeFailed,"write/flush failure marks incomplete session");
    }
    std::cout<<"FPGA archive software tests passed; USB hardware not exercised\n";
    return 0;
}
