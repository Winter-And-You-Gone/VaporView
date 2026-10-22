#include "test_settings_sandbox.h"
#include "WaterCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <cmath>
#include <functional>
#include <stdexcept>

using namespace VaporView;
using namespace VaporView::Ground;
using namespace VaporView::Ground::Calibration;
using WaterCalibrationTest::read;
using WaterCalibrationTest::recordSession;

namespace
{
void require(bool condition, const char *message) { if(!condition) throw std::runtime_error(message); }
bool closeTo(double a,double b) {return std::abs(a-b)<1e-10;}
template<class T> T *control(QWidget& page,const char *name)
{auto *w=page.findChild<T*>(QString::fromLatin1(name));require(w,"named page control");return w;}
void ready(QWidget& page,const std::function<bool()>& condition,bool error=false)
{
    QElapsedTimer elapsed;elapsed.start();
    const auto complete=[&]{return control<QPushButton>(page,"calibrationCalculate")->isEnabled()&&condition();};
    while(!complete()&&elapsed.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}
    require(complete(),qPrintable(control<QLabel>(page,"calibrationStatus")->text()));
    require(control<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool()==error,"expected operation status");
}
void click(QWidget& page,const char *name)
{auto *b=control<QPushButton>(page,name);require(b->isEnabled(),"operation enabled");b->click();}
void write(const QString& filename,const QByteArray& bytes)
{QFile f(filename);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(bytes)==bytes.size(),"write test fixture");}
const ConcentrationResult& resultFor(const ConcentrationSnapshot& snapshot,int wave,quint64 sequence)
{
    for(const auto& r:snapshot.results)if(r.wavelengthNm==wave&&r.input.feature.waveformSequence==sequence)return r;
    throw std::runtime_error("missing exact waveform result identity");
}
void send(Ground::Session::GroundRecordingService& recorder,quint16 channel,quint32 cycle,int peak,quint64 timestamp,int fault=0)
{
    FpgaWave::CompletedStream wave;wave.source=channel;wave.message=0x1001;wave.schema=2;wave.format=0;
    wave.complete=true;wave.totalPoints=3;wave.bytesPerPoint=8;wave.rate=10000;wave.cycleId=cycle;
    wave.timestamp=0xfedcba9876543210ULL+cycle;wave.overflow=fault==2;
    for(int i=0;i<3;++i){FpgaWave::CompletedStream::DliaPoint p;p.hasHarmonics=true;p.h1=fault==1?0:(i==0?100:2);p.h2=i==0?peak:peak*2;wave.dliaPoints.append(p);}
    require(recorder.recordFpgaHarmonics(timestamp,wave),"admit synthetic complete FPGA cycle into production recorder");
}
ConcentrationSnapshot save(WaterCalibrationPage& page,const QString& filename,int count)
{
    page.exportConcentrationResults(filename);ready(page,[&]{return QFile::exists(filename);});
    auto snapshot=readResults(filename);require(snapshot.success,qPrintable(snapshot.error));
    require(snapshot.results.size()==count,"persist every result exactly once");
    for(const auto& r:snapshot.results)require(r.origin=="software_test"&&!r.compensated&&
        r.toJson()["compensation_status"].toString().contains(QStringLiteral("未进行温压补偿")),"synthetic and uncompensated provenance survives persistence");
    return snapshot;
}

void run()
{
    QTemporaryDir temp;require(temp.isValid(),"temporary test scope");
    const QStringList sources{recordSession(temp.filePath("low"),2),recordSession(temp.filePath("mid"),4),
        recordSession(temp.filePath("high"),6),recordSession(temp.filePath("independent_repeat"),2)};
    const auto rawBefore=read(sources[0]+"/raw/waveform.dat"),featuresBefore=read(sources[0]+"/sensors/waveform_features.csv");
    auto data=Dataset::create("E2E SOFTWARE TEST ONLY",816,0x31,"ppmv");
    const QVector<double> concentrations{5,9,12,5};
    for(int i=0;i<sources.size();++i){const auto imported=importSession(sources[i],data,concentrations[i],false);
        require(imported.success&&imported.samples.size()==1,"A: actual-format Session import and channel filtering");data.samples+=imported.samples;}
    require(importSession(sources[0],data,5,true).duplicates==1&&data.samples.size()==4,"A: deduplicate same period, retain independent equal-concentration measurement");
    auto model=FitModel::create("linear",1);data.revision=1;const auto fitted=model->fit(data);require(fitted.success,"A: linear fit succeeds");
    const auto v1=fitted.fit;
    require(closeTo(v1.a,39.0/22)&&closeTo(v1.b,17.0/11)&&closeTo(v1.rSquared,1521.0/1529)&&closeTo(v1.rmse,std::sqrt(1.0/22))&&
        v1.sampleCount==4&&v1.concentrationCount==3&&v1.rMin==2&&v1.rMax==6&&v1.cMin==5&&v1.cMax==12,
        "A: independent weighted-by-cycle OLS coefficients, statistics and range");
    const QVector<double> residuals{-1.0/11,4.0/11,-2.0/11,-1.0/11};
    require(v1.residuals.size()==residuals.size(),"A: residual count follows participating rows");
    for(int i=0;i<residuals.size();++i)require(closeTo(v1.residuals[i],residuals[i]),"A: independently expected per-row residual");
    auto excluded=data;excluded.samples[1].excluded=true;require(model->fit(excluded).fit.sampleCount==3,"A: excluded row leaves fit statistics");
    auto edited=data;edited.revision=2;for(auto& s:edited.samples)s.concentration=4*s.feature.peak+2;
    const auto v2=model->fit(edited).fit;require(v2.a==4&&v2.b==2&&v1.dataset.samples[0].concentration==5,"C: editing creates different coefficients without mutating v1");
    auto infrared=Dataset::create("1368 E2E SOFTWARE TEST",1368,0x30,"ppmv");infrared.revision=1;
    for(int i=0;i<3;++i)infrared.samples+=importSession(sources[i],infrared,3*(i*2+2)+4,true).samples;
    const auto ir=model->fit(infrared).fit;require(ir.a==3&&ir.b==4,"A: independent 1368 model");
    const auto filename=temp.filePath("library.json");Store store(filename);Library library;
    library.datasets={data,edited,infrared};library.fits={v1,v2,ir};library.active={{816,v1.id},{1368,ir.id}};
    QString error;require(store.save(library,&error),qPrintable(error));Library reopened;
    require(Store(filename).load(reopened,&error)&&reopened.activeFit(816)->id==v1.id,"A: fresh service loads exact saved active version");
    require(closeTo(calculate(data.samples[0],816,reopened.activeFit(816)).value,56.0/11),"A: restored coefficients predict independently expected value");
    auto singular=Dataset::create("DEGENERATE SOFTWARE TEST",816,0x31,"ppmv");
    singular.samples=importSession(sources[0],singular,5,true).samples;singular.samples+=importSession(sources[3],singular,9,true).samples;
    require(!model->fit(singular).success&&!model->fit(Dataset::create("empty",816,0x31,"ppmv")).success,"D: genuinely constant recorded peaks and insufficient data fail");
    const auto storedLibrary=read(filename);write(filename,"{damaged");
    require(!store.load(reopened,&error)&&reopened.activeFit(816)->id==v1.id,"D: corrupted reload cannot discard the existing version");write(filename,storedLibrary);

    Ground::Session::GroundRecordingService recorder;Ground::Session::GroundRecordingOptions options;
    options.baseDirectory=temp.filePath("live");options.fpgaConfiguration={{"backend","simulator"}};
    require(recorder.start(options),"B: start real recording service");const auto firstSession=recorder.status().sessionDirectory;
    const auto timestamp=Ground::Session::GroundRecordingService::currentTimestampUs();
    const auto snapshotFile=temp.filePath("v1-results.json");
    {
        WaterCalibrationPage page(nullptr,filename);page.resize(1200,900);page.show();
        ready(page,[&]{return control<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("定标库已加载"));});
        auto *tabs=page.findChild<QTabWidget*>();require(tabs&&tabs->count()==3,"existing three-tab navigation");tabs->setCurrentIndex(2);
        auto *follow=control<QCheckBox>(page,"calibrationFollow");auto *timer=page.findChild<QTimer*>();
        require(follow->isChecked()&&timer&&timer->interval()==2000,"B: automatic follow is the actual default two-second cadence");
        control<QLineEdit>(page,"calibrationSession")->setText(firstSession);
        send(recorder,0x31,0,3,timestamp);send(recorder,0x30,0,4,timestamp+1);
        auto *table=control<QTableWidget>(page,"calibrationResults");ready(page,[&]{return table->rowCount()==2;});
        require(table->item(0,6)->text().startsWith(QStringLiteral("软件测试数据"))&&
            table->fontMetrics().horizontalAdvance(table->item(0,5)->text())+12<=table->columnWidth(5),
            "B: visible software-test provenance and uncompensated status");
        auto saved=save(page,snapshotFile,2);const auto originalBytes=read(snapshotFile);
        require(closeTo(resultFor(saved,816,0).value,151.0/22)&&resultFor(saved,1368,1).value==16,"B: distinct channel peaks use the fixed 816/DLIA1 and 1368/DLIA0 models");
        const auto features=SessionWaveformFeatures::read(firstSession);require(features.success&&features.records.size()==2,"B: complete recorded feature rows visible");
        for(const auto& r:saved.results){bool matched=false;for(const auto& f:features.records)if(f.id==r.input.feature.id){matched=true;
            require(r.input.feature.toJson()==f.toJson()&&r.input.feature.channel==(r.wavelengthNm==816?0x31:0x30)&&
                r.input.feature.hostTimeUs==(r.wavelengthNm==816?timestamp:timestamp+1),"B: exact fixed channel/Session/time/record mapping, not nearest-time guessing");}
            require(matched,"B: every output has an original feature");}
        QProcess fresh;fresh.start(QCoreApplication::applicationFilePath(),{"--read-snapshot",snapshotFile});
        require(fresh.waitForFinished(10000)&&fresh.exitStatus()==QProcess::NormalExit&&fresh.exitCode()==0,"A/B: independent process reads persisted results and parameters without a calibration library");
        timer->setInterval(20);
        tabs->setCurrentIndex(1);auto *versions=control<QComboBox>(page,"calibrationVersions");versions->setCurrentIndex(versions->findData(1));
        QMetaObject::invokeMethod(versions,"activated",Qt::DirectConnection,Q_ARG(int,versions->currentIndex()));
        click(page,"calibrationEnable");ready(page,[&]{return control<QLabel>(page,"calibrationCalculationSummary")->text().contains(v2.id);});
        tabs->setCurrentIndex(2);
        ready(page,[&]{return table->rowCount()==2;});auto switched=save(page,temp.filePath("v2-results.json"),2);
        require(resultFor(switched,816,0).value==14&&resultFor(switched,816,0).calibrationId==v2.id&&
            resultFor(switched,1368,1).calibrationId==ir.id,"C: switch changes 816 coefficients and preserves 1368");
        require(read(snapshotFile)==originalBytes&&resultFor(readResults(snapshotFile),816,0).calibrationId==v1.id,"C: saved history and original parameters remain unchanged");
        page.loadConcentrationResults(snapshotFile);ready(page,[&]{return control<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("已读取"));});
        require(!follow->isChecked()&&closeTo(table->item(0,3)->text().toDouble(),151.0/22),"C: history view pauses follow and shows the original result");
        save(page,temp.filePath("history-copy.json"),2);require(read(temp.filePath("history-copy.json"))==originalBytes,"C: exporting loaded history never substitutes current active parameters or calculation time");
        tabs->setCurrentIndex(1);click(page,"calibrationDisable");ready(page,[&]{return !control<QLabel>(page,"calibrationCalculationSummary")->text().contains(v2.id);});tabs->setCurrentIndex(2);
        send(recorder,0x31,1,3,timestamp+2);follow->setChecked(true);ready(page,[&]{return table->rowCount()==3;});
        const auto disabled=save(page,temp.filePath("disabled.json"),3);
        require(!resultFor(disabled,816,2).valid&&resultFor(disabled,816,2).status.contains(QStringLiteral("未启用")),"C: disabled version never validates subsequent results");
        require(recorder.pause(),"B: pause drains recording");
        FpgaWave::CompletedStream late;require(!recorder.recordFpgaHarmonics(timestamp,late),"B: paused input rejected");
        require(recorder.start(options),"B: resume same Session");send(recorder,0x31,1,4,timestamp+2);
        send(recorder,0x30,1,7,timestamp+3);send(recorder,0x30,2,3,timestamp+4,2);
        ready(page,[&]{return table->rowCount()==6;});tabs->setCurrentIndex(1);click(page,"calibrationEnable");
        ready(page,[&]{return control<QLabel>(page,"calibrationCalculationSummary")->text().contains(v2.id);});
        tabs->setCurrentIndex(2);
        send(recorder,0x31,2,3,timestamp+5,1);ready(page,[&]{return table->rowCount()==7;});
        const auto faults=save(page,temp.filePath("faults.json"),7);
        require(!resultFor(faults,1368,4).valid&&resultFor(faults,1368,4).status.contains(QStringLiteral("超出"))&&
            !resultFor(faults,1368,5).valid&&!resultFor(faults,816,6).valid,"D: out-of-range, overflow and invalid denominator produce explicit invalid records");
        require(faults.results.size()==7&&resultFor(faults,816,3).input.feature.segmentId!=resultFor(faults,816,2).input.feature.segmentId,
            "B: resumed repeated cycle and timestamp remain distinct, without duplicate accumulation");
        require(!calculate(data.samples[0],1368,&v2).valid,"D: channel/wavelength mismatch rejected");
        require(!recorder.stop().writeFailed,"B: stop seals source");const auto sealed=read(firstSession+"/sensors/waveform_features.csv");
        require(!recorder.recordFpgaHarmonics(timestamp,late)&&read(firstSession+"/sensors/waveform_features.csv")==sealed,"B: late callbacks cannot write to sealed source");
        options.baseDirectory=temp.filePath("second-live");require(recorder.start(options),"B: second Session");const auto secondSession=recorder.status().sessionDirectory;
        send(recorder,0x31,0,4,timestamp+6);send(recorder,0x30,0,4,timestamp+7);require(!recorder.stop().writeFailed,"B: second Session sealed");
        follow->setChecked(false);click(page,"calibrationCalculate");control<QLineEdit>(page,"calibrationSession")->setText(secondSession);
        ready(page,[]{return true;});require(table->rowCount()==0,"D: pending old-source task cannot refill the switched Session");
        follow->setChecked(true);ready(page,[&]{return table->rowCount()==2;});const auto second=save(page,temp.filePath("second.json"),2);
        for(const auto& r:second.results)require(r.input.feature.sessionDirectory==secondSession,"B: switched source cannot inherit old results");
        require(read(firstSession+"/sensors/waveform_features.csv")==sealed&&read(snapshotFile)==originalBytes,"B/C: old source and saved results unchanged by new Session");
    }
    {
        WaterCalibrationPage restarted(nullptr,temp.filePath("no-library.json"));
        ready(restarted,[&]{return control<QLabel>(restarted,"calibrationStatus")->text().contains(QStringLiteral("定标库已加载"));});
        restarted.loadConcentrationResults(snapshotFile);ready(restarted,[&]{return control<QTableWidget>(restarted,"calibrationResults")->rowCount()==2;});
        require(closeTo(control<QTableWidget>(restarted,"calibrationResults")->item(0,3)->text().toDouble(),151.0/22),"A/B: reopened application view reads self-contained original results");
        write(temp.filePath("bad.json"),"{damaged");restarted.loadConcentrationResults(temp.filePath("bad.json"));ready(restarted,[]{return true;},true);
        require(!readResults(temp.filePath("bad.json")).success,"D: corrupted history rejected with explicit UI error");
        control<QLineEdit>(restarted,"calibrationSession")->setText(firstSession);click(restarted,"calibrationCalculate");
        // Destroy while the worker still owns the queued calculation. No source
        // writes exist in this path; QObject context disconnects the UI callback.
    }
    require(QThreadPool::globalInstance()->waitForDone(15000),"D: pending task drains safely after page destruction");QApplication::processEvents();
    auto root=QJsonDocument::fromJson(read(snapshotFile)).object();root["results"]=QJsonArray();write(temp.filePath("corrupt-digest.json"),QJsonDocument(root).toJson());
    require(!readResults(temp.filePath("corrupt-digest.json")).success,"D: checksum detects changed persistent results");
    root=QJsonDocument::fromJson(read(snapshotFile)).object();auto rows=root["results"].toArray();auto wrong=rows[0].toObject();
    wrong["concentration"]=999;rows[0]=wrong;root["results"]=rows;root.remove("sha256");
    root["sha256"]=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    write(temp.filePath("wrong-result.json"),QJsonDocument(root).toJson());
    require(!readResults(temp.filePath("wrong-result.json")).success,"D: coherent checksum cannot replace validation against the original parameters");
    root=QJsonDocument::fromJson(read(snapshotFile)).object();auto fits=root["calibrations"].toArray();auto bad=fits[0].toObject();bad["model_version"]=99;fits[0]=bad;root["calibrations"]=fits;root.remove("sha256");
    root["sha256"]=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    write(temp.filePath("unknown-model.json"),QJsonDocument(root).toJson());require(!readResults(temp.filePath("unknown-model.json")).success,"D: semantic model check remains after valid checksum");
    auto legacyRow=resultFor(readResults(snapshotFile),816,0).toJson();legacyRow.remove("compensation_status");
    write(temp.filePath("legacy.json"),QJsonDocument(QJsonObject{{"schema_version",1},{"kind","water_initial_concentrations"},{"results",QJsonArray{legacyRow}}}).toJson());
    require(!readResults(temp.filePath("legacy.json")).success&&readResults(temp.filePath("legacy.json"),library.fits).success,"D: old exports require exact historical parameters, never current-version substitution");
    require(read(sources[0]+"/raw/waveform.dat")==rawBefore&&read(sources[0]+"/sensors/waveform_features.csv")==featuresBefore,"A-D: original waveform and feature files are read-only");
}
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    try {
        if(app.arguments().size()==3&&app.arguments()[1]=="--read-snapshot"){
            const auto snapshot=readResults(app.arguments()[2]);require(snapshot.success,qPrintable(snapshot.error));
            require(snapshot.calibrations.size()==2&&snapshot.results.size()==2&&closeTo(resultFor(snapshot,816,0).value,151.0/22),"fresh process independently validates complete historical model");return 0;
        }
        run();std::cout<<"water_calibration_e2e_test passed: A-D SOFTWARE TEST DATA ONLY\n";return 0;
    } catch(const std::exception& e) {QThreadPool::globalInstance()->waitForDone(15000);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
