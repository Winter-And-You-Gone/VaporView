#include "test_settings_sandbox.h"
#include "WaterCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QPushButton>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThreadPool>

using namespace WaterCalibrationTest;
using namespace VaporView::Ground::Calibration;
namespace {
void write(const QString& path,const QByteArray& bytes){QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size(),"write SOFTWARE TEST fixture");}
template<class T> T* ui(QWidget& page,const char* name){auto* w=page.findChild<T*>(name);check(w,"named joint workflow control");return w;}
void wait(QWidget& page,bool error=false){QElapsedTimer timer;timer.start();while(!ui<QPushButton>(page,"calibrationCalculate")->isEnabled()&&timer.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}check(ui<QPushButton>(page,"calibrationCalculate")->isEnabled(),"bounded UI work");check(ui<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool()==error,qPrintable(ui<QLabel>(page,"calibrationStatus")->text()));}
void click(QWidget& p,const char* name){ui<QPushButton>(p,name)->click();wait(p);}
void source(QWidget& page,const QString& session,double concentration){ui<QLineEdit>(page,"calibrationSession")->setText(session);ui<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(concentration);ui<QCheckBox>(page,"calibrationSingleConcentration")->setChecked(true);click(page,"calibrationImport");}
void process(){for(int i=0;i<10;++i){QApplication::processEvents();QThread::msleep(5);}}
void core(const QString& root,const QStringList& sessions){
    auto task=CalibrationTask::create("JOINT SOFTWARE TEST ONLY","ppmv");
    check(task.validate().isEmpty()&&task.datasets.size()==2&&task.datasets[816].id!=task.datasets[1368].id,"one stable task with distinct dataset identities");
    check(!importTaskSession(sessions[0],task,5,true,false).success,"unconfirmed Session concentration interval is rejected");
    for(int i=0;i<3;++i){const auto batch=importTaskSession(sessions[i],task,5+4*i,true,true);check(batch.success&&batch.channels.size()==2,"one import returns both channels");
        for(const int wave:{816,1368}){const auto& rows=batch.channels[wave].samples;check(rows.size()==1&&rows[0].concentration==5+4*i&&rows[0].feature.channel==channelForWavelength(wave),"one concentration associated with both fixed channels");
            const auto features=VaporView::Ground::SessionWaveformFeatures::read(sessions[i]);bool exact=false;for(const auto& feature:features.records)if(feature.id==rows[0].feature.id)exact=feature.toJson()==rows[0].feature.toJson();check(exact,"sequence, timestamp, peak location and quality preserved exactly");task.datasets[wave].samples+=rows;}
    }
    const auto duplicate=importTaskSession(sessions[0],task,5,true,true);
    check(duplicate.success&&duplicate.channels[816].duplicates==1&&duplicate.channels[1368].duplicates==1,"deduplication is independent for both channels");
    const auto conflict=importTaskSession(sessions[0],task,99,true,true);check(!conflict.success&&conflict.channels.isEmpty(),"different concentration on identical records is rejected atomically");
    const auto repeated=recordSession(root+"/repeat",2,false,1);const auto independent=importTaskSession(repeated,task,5,true,true);
    check(independent.success&&independent.channels[816].samples.size()==1&&independent.channels[1368].samples.size()==1,"equal peaks in independent measurements are retained");
    auto oneSided=CalibrationTask::create("ONE SIDED SOFTWARE TEST","ppmv");oneSided.datasets[816].samples=task.datasets[816].samples;
    Store oneStore(root+"/one-sided-library.json");const auto oneFit=saveTask(oneSided,Library{},oneStore,816);
    check(oneFit.success,"one wavelength can fit while its peer has no samples");const auto oneBytes=read(oneStore.filename());
    const auto noFit=saveTask(oneFit.task,oneFit.library,oneStore,1368);
    check(!noFit.success&&noFit.error.contains("1368")&&read(oneStore.filename())==oneBytes,"peer fitting failure identifies its wavelength and preserves the successful version");
    Store store(root+"/joint-library.json");Library library;
    auto saved=saveTask(task,library,store);check(saved.success,qPrintable(saved.error));task=saved.task;library=saved.library;
    const auto first=saveTask(task,library,store,816);check(first.success&&first.fit->a==2&&first.fit->b==1,"816 independent coefficients");task=first.task;library=first.library;
    const auto second=saveTask(task,library,store,1368);check(second.success&&second.fit->a==4&&second.fit->b==1,"1368 independent coefficients from the same standard concentrations");task=second.task;library=second.library;
    check(first.fit->rMin==2&&first.fit->rMax==6&&second.fit->rMin==1&&second.fit->rMax==3&&first.fit->rmse==0&&second.fit->rSquared==1,"independent ranges and fit statistics");
    library.active={{816,first.fit->id},{1368,second.fit->id}};QString error;check(store.save(library,&error),qPrintable(error));
    const auto other=library.fits.last().toJson();task.datasets[816].samples[0].excluded=true;
    const auto changed=saveTask(task,library,store,816);check(changed.success&&changed.fit->sampleCount==2&&changed.library.fits[1].toJson()==other&&changed.library.active==library.active,"one-channel edits cannot change the other coefficients or either enabled version");
    library=changed.library;task=changed.task;const auto bytes=read(store.filename());QLockFile lock(store.filename()+".lock");check(lock.tryLock(),"hold store lock");
    task.datasets[816].name=task.datasets[1368].name="pending SOFTWARE TEST";
    const auto failed=saveTask(task,library,store);check(!failed.success&&read(store.filename())==bytes,"failed atomic save leaves both stored datasets unchanged");lock.unlock();
    Library reopened;check(store.load(reopened,&error)&&reopened.datasets.size()==library.datasets.size(),"joint revisions reload");
    // Produce a real schema-v1 library without the new task_id field.
    auto oldData=first.fit->dataset;oldData.taskId.clear();oldData.revision=1;auto oldFit=FitModel::create("linear",1)->fit(oldData).fit;
    QJsonObject payload{{"schema_version",1},{"datasets",QJsonArray{oldData.toJson()}},{"fits",QJsonArray{oldFit.toJson()}},{"active",QJsonObject{{"816",oldFit.id}}}};
    const auto digest=QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
    const auto oldFile=root+"/schema-v1.json";write(oldFile,QJsonDocument(QJsonObject{{"payload",payload},{"sha256",QString::fromLatin1(digest)}}).toJson());
    Store oldStore(oldFile);Library old;check(oldStore.load(old,&error)&&old.datasets[0].toJson()==oldData.toJson()&&old.fits[0].toJson()==oldFit.toJson(),"schema-v1 data and historical parameters remain byte-equivalent JSON");
    const auto oldSnapshot=root+"/old-results.json";check(exportResults(oldSnapshot,{calculate(oldData.samples[0],816,&oldFit)},&error,{oldFit})&&readResults(oldSnapshot).success,"old concentration snapshot remains independently readable");
    const auto migrated=saveTask(CalibrationTask::create("new alongside legacy","ppmv"),old,oldStore);
    check(migrated.success&&migrated.library.datasets[0].toJson()==oldData.toJson()&&migrated.library.fits[0].toJson()==oldFit.toJson(),"migration appends paired task without rewriting history");
    check(QJsonDocument::fromJson(read(oldFile)).object()["payload"].toObject()["schema_version"].toInt()==2,"successful atomic save writes schema-v2");
    // Missing and bad-quality channels are reported independently.
    const auto partial=recordSession(root+"/partial",2);const auto path=partial+"/sensors/waveform_features.csv";auto lines=read(path).split('\n');QByteArray only=lines[0]+'\n';
    for(int i=1;i<lines.size();++i)if(!lines[i].isEmpty()&&lines[i].split(',')[5]=="49")only+=lines[i]+'\n';write(path,only);
    const auto missing=importTaskSession(partial,CalibrationTask::create("partial test","ppmv"),5,true,true);
    check(missing.success&&missing.channels[816].samples.size()==1&&missing.channels[1368].samples.isEmpty()&&!missing.channels[1368].warning.isEmpty(),"missing channel is never copied from its peer");
    auto corrupt=only+"1,2\n";write(path,corrupt);const auto bad=importTaskSession(partial,CalibrationTask::create("bad test","ppmv"),5,true,true);
    check(!bad.success&&bad.channels.isEmpty(),"completed corrupt CSV fails before either channel changes");
    const auto faults=recordSession(root+"/faults",2,true);const auto quality=importTaskSession(faults,CalibrationTask::create("quality test","ppmv"),5,true,true);
    check(quality.success&&quality.channels[816].invalid==2&&quality.channels[1368].invalid==2,"existing per-cycle quality rules retained independently");
    const auto faultFile=faults+"/sensors/waveform_features.csv";auto faultLines=read(faultFile).split('\n');
    const int qualityColumn=faultLines[0].split(',').indexOf("quality_flags");QByteArray allBad=faultLines[0]+'\n';
    for(int i=1;i<faultLines.size();++i)if(!faultLines[i].isEmpty()){
        auto fields=faultLines[i].split(',');if(fields[5]=="48")fields[qualityColumn]="2";
        allBad+=fields.join(',')+'\n';
    }
    write(faultFile,allBad);const auto rejected=importTaskSession(faults,CalibrationTask::create("ALL BAD SOFTWARE TEST","ppmv"),5,true,true);
    check(rejected.success&&rejected.channels[1368].invalid==4&&rejected.channels[816].invalid==2&&
        rejected.channels[1368].warning.contains(QStringLiteral("全部质量不合格")),"all-invalid peer retains only its own provenance with an explicit partial-success report");
}
void page(const QString& root,const QStringList& sessions){
    const auto filename=root+"/ui-library.json";WaterCalibrationPage page(nullptr,filename);page.resize(1200,1000);page.show();process();wait(page);
    ui<QCheckBox>(page,"calibrationFollow")->setChecked(false);
    check(!page.findChild<QComboBox*>("calibrationWavelength")&&ui<QTabBar>(page,"calibrationWavelengthViews")->count()==2,"new joint task needs no wavelength selection");
    ui<QLineEdit>(page,"calibrationSession")->setText(sessions[0]);ui<QPushButton>(page,"calibrationImport")->click();wait(page,true);
    check(ui<QTableWidget>(page,"calibrationSamples")->rowCount()==0,"unconfirmed mixed-concentration input cannot silently import");
    for(int i=0;i<3;++i)source(page,sessions[i],5+4*i);
    check(ui<QTableWidget>(page,"calibrationSamples")->rowCount()==3&&ui<QLabel>(page,"calibrationImportSummary")->text().contains("1368"),"single import gives a simultaneous two-channel report");
    click(page,"calibrationFit");click(page,"calibrationEnable");
    auto* views=ui<QTabBar>(page,"calibrationWavelengthViews");views->setCurrentIndex(1);
    check(ui<QTableWidget>(page,"calibrationSamples")->rowCount()==3,"1368 already has the same imported standard batches");click(page,"calibrationFit");click(page,"calibrationEnable");
    Library library;QString error;check(Store(filename).load(library,&error)&&library.activeFit(816)&&library.activeFit(1368)&&library.activeFit(816)->a==2&&library.activeFit(1368)->a==4,"UI saves and enables independent curves");
    const auto peer=library.activeFit(1368)->toJson();views->setCurrentIndex(0);ui<QTableWidget>(page,"calibrationSamples")->setCurrentCell(0,0);click(page,"calibrationExclude");
    views->setCurrentIndex(1);check(ui<QLabel>(page,"calibrationSampleSummary")->text().contains("有效 3"),"816 exclusion leaves 1368 samples and fit view intact");
    views->setCurrentIndex(0);click(page,"calibrationFit");click(page,"calibrationDisable");check(Store(filename).load(library,&error)&&!library.activeFit(816)&&library.activeFit(1368)->toJson()==peer,"independent fitting and disable preserve the peer enabled snapshot");
    const auto prediction=recordSession(root+"/prediction",3,false,2);ui<QLineEdit>(page,"calibrationSession")->setText(prediction);click(page,"calibrationCalculate");
    auto* results=ui<QTableWidget>(page,"calibrationResults");check(results->rowCount()==2,"existing concentration calculation still reads both channels");
    for(int row=0;row<2;++row){if(results->item(row,0)->text()=="816")check(results->item(row,3)->text()==QStringLiteral("—"),"disabled wavelength invalid alone");else check(results->item(row,3)->text()=="9"&&results->item(row,5)->text().contains(QStringLiteral("未进行温压补偿")),"peer concentration remains valid and uncompensated");}
    click(page,"calibrationEnable");click(page,"calibrationCalculate");
    const auto snapshot=root+"/joint-results.json";page.exportConcentrationResults(snapshot);wait(page);check(readResults(snapshot).success,"paired datasets preserve independent concentration snapshots");
    const auto oldBytes=read(filename);ui<QLineEdit>(page,"calibrationSession")->setText(sessions[0]);
    ui<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(99);
    ui<QCheckBox>(page,"calibrationSingleConcentration")->setChecked(true);ui<QPushButton>(page,"calibrationImport")->click();wait(page,true);
    check(read(filename)==oldBytes&&results->rowCount()==0,"conflicting import preserves storage and cannot keep a previous source concentration display");
    {
        WaterCalibrationPage reopened(nullptr,filename);reopened.resize(1200,1000);reopened.show();process();wait(reopened);
        ui<QCheckBox>(reopened,"calibrationFollow")->setChecked(false);
        auto* tasks=ui<QComboBox>(reopened,"calibrationDatasets");check(tasks->count()==2,"one grouped task survives page reinitialization");
        tasks->setCurrentIndex(1);QMetaObject::invokeMethod(tasks,"activated",Q_ARG(int,1));
        auto* reopenedViews=ui<QTabBar>(reopened,"calibrationWavelengthViews");
        for(int index=0;index<2;++index){reopenedViews->setCurrentIndex(index);bool fitted=false;
            for(auto* label:reopened.findChildren<QLabel*>())fitted|=label->text().contains("RMSE");
            check(ui<QTableWidget>(reopened,"calibrationSamples")->rowCount()==3&&fitted,"both datasets and independent fits restore from the grouped task");}
        check(ui<QComboBox>(reopened,"calibrationUnit")->currentText()=="ppmv","shared concentration unit restores");
        ui<QTableWidget>(reopened,"calibrationSamples")->setCurrentCell(0,0);click(reopened,"calibrationExclude");
        QLockFile lock(filename+".lock");check(lock.tryLock(),"hold UI store lock");
        ui<QPushButton>(reopened,"calibrationSave")->click();wait(reopened,true);lock.unlock();
        check(read(filename)==oldBytes&&ui<QLabel>(reopened,"calibrationSampleSummary")->text().contains("已排除 1"),"failed page save preserves both disk bytes and pending edits");
    }
}
}
int main(int argc,char** argv){QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"isolated SOFTWARE TEST DATA ONLY");QStringList sessions;for(int i=1;i<=3;++i)sessions<<recordSession(temp.filePath(QString::number(i)),i*2,false,i);core(temp.path(),sessions);page(temp.path(),sessions);check(QThreadPool::globalInstance()->waitForDone(15000),"bounded workers drain");std::cout<<"water_calibration_joint_test passed (SOFTWARE TEST DATA ONLY)\n";}
