#include "WaterCalibrationFixture.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionWaveformRepository.h"
#include "shared/session/SessionPackageLayout.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>

using namespace VaporView::Ground;
using namespace VaporView::Ground::Calibration;
using namespace WaterCalibrationTest;
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==4&&app.arguments()[1]=="--reload-library") {
        Library library;QString error;check(Store(app.arguments()[2]).load(library,&error),qPrintable(error));
        const auto *fit=library.activeFit(816);
        check(fit&&fit->id==app.arguments()[3]&&!library.activeFit(1368),"new process loads exact active version");
        check(calculate(fit->dataset.samples.first(),816,fit).value==5,"new process predicts from saved snapshot");return 0;
    }
    QTemporaryDir temp;check(temp.isValid(),"temporary test scope");
    const auto first=recordSession(temp.filePath("first"),2,true), second=recordSession(temp.filePath("second"),4);
    const auto rawBefore=read(first+"/raw/waveform.dat"), featuresBefore=read(first+"/sensors/waveform_features.csv");
    // Existing sensor-summary format: timestamps are record snapshots, not an
    // asserted physical sensor acquisition time. No invented compensation.
    const auto sensorFile=QDir(first).filePath(VaporView::Session::standardSessionPackageLayout().sensorSummaryCsvPath);
    QFile sensors(sensorFile);check(sensors.open(QIODevice::WriteOnly),"write software-test sensor snapshot");
    sensors.write("record_timestamp_us,hmp_temperature_c,ptb_pressure_hpa\n1700000000000000,23.5,1001.2\n");sensors.close();
    auto dataset=Dataset::create("SOFTWARE TEST ONLY",816,0x31,"ppmv");
    check(channelForWavelength(816)==0x31&&channelForWavelength(1368)==0x30&&channelForWavelength(0)==0,
        "confirmed hardware mapping is 816/DLIA1 and 1368/DLIA0");
    const auto wrongMapping=importSession(first,Dataset::create("OLD MAPPING SOFTWARE TEST",816,0x30,"ppmv"),5,true);
    check(!wrongMapping.success&&wrongMapping.samples.isEmpty()&&wrongMapping.error.contains(QStringLiteral("固定对应")),
        "wrong hardware mapping cannot import or relabel the other channel");
    const auto imported=importSession(first,dataset,5,false);
    check(imported.success&&imported.samples.size()==4&&imported.invalid==2,"map actual CSV, retain bad-quality rows");
    dataset.samples=imported.samples;
    check(dataset.samples.first().feature.channel==0x31,"816 imports the confirmed DLIA1 feature records");
    check(dataset.samples[0].feature.peak==2&&dataset.samples[0].feature.peakIndex==1,"use max(H2/H1), not max(H2)/max(H1)");
    check(dataset.samples[0].feature.fpgaTick==0xfedcba9876543210ULL&&dataset.samples[0].feature.waveformSequence==4,"lossless tick and record identity");
    check(dataset.samples[1].feature.qualityFlags==1&&dataset.samples[1].feature.invalidReason().isEmpty(),"skip invalid point but retain valid-cycle peak");
    check(dataset.samples[2].feature.invalidReason().size()>0&&dataset.samples[3].feature.invalidReason().size()>0,"reject no-valid and overflow cycles");
    check(dataset.samples[0].origin=="software_test","detect recorded simulator provenance");
    check(dataset.samples[0].environment.temperatureC==23.5&&dataset.samples[0].environment.pressureHpa==1001.2&&
        dataset.samples[0].environment.snapshotTimeUs==1700000000000000ULL&&dataset.samples[0].environment.deltaUs==0x31,"reuse existing sensor time alignment and retain snapshot timestamp");
    check(!std::isfinite(importSession(second,dataset,9,true).samples.first().environment.temperatureC),"missing temperature stays missing");
    auto model=FitModel::create("linear",1);check(bool(model),"versioned model factory");
    check(!model->fit(Dataset::create("empty",816,0x31,"ppmv")).success,"reject insufficient samples");
    check(!model->fit(dataset).success,"reject one concentration level");
    auto more=importSession(second,dataset,9,true);check(more.success&&more.samples.size()==1,"import another known standard");dataset.samples+=more.samples;
    auto fitted=model->fit(dataset);check(fitted.success&&std::abs(fitted.fit.a-2)<1e-12&&std::abs(fitted.fit.b-1)<1e-12,"synthetic linear coefficients C=2R+1");
    check(fitted.fit.sampleCount==3&&fitted.fit.concentrationCount==2&&fitted.fit.rSquared==1&&fitted.fit.rmse==0,"quality exclusion and fit metrics");
    const auto duplicate=importSession(first,dataset,123,true);check(duplicate.success&&duplicate.samples.isEmpty()&&duplicate.duplicates==4,"deduplicate by original Session/channel/sequence");
    const auto copied=temp.filePath("copy");check(QDir().mkpath(copied+"/sensors"),"copy fixture directory");
    check(QFile::copy(first+"/session.json",copied+"/session.json")&&QFile::copy(first+"/sensors/waveform_features.csv",copied+"/sensors/waveform_features.csv"),"copy existing Session identity and features");
    const auto relocated=importSession(copied,dataset,5,true);
    check(relocated.success&&relocated.samples.isEmpty()&&relocated.duplicates==4,"copying or moving standard Session does not duplicate original features");
    const auto predictionFit=fitted.fit;
    auto sample=dataset.samples.first();auto concentration=calculate(sample,816,&predictionFit);
    check(concentration.valid&&concentration.value==5&&!concentration.compensated&&concentration.status.contains(QStringLiteral("未进行温压补偿")),"known prediction, explicitly uncompensated");
    check(concentration.origin=="software_test"&&!calculate(sample,1368,&fitted.fit).valid,"software-test label and wavelength isolation");
    sample.feature.channel=0x30;check(!calculate(sample,816,&fitted.fit).valid,"DLIA isolation");sample=dataset.samples.first();
    sample.feature.peak=5;check(!calculate(sample,816,&fitted.fit).valid,"no extrapolation above range");sample.feature.peak=1;check(!calculate(sample,816,&fitted.fit).valid,"no extrapolation below range");
    check(!calculate(dataset.samples[2],816,&fitted.fit).valid&&!calculate(dataset.samples.first(),816,nullptr).valid,"invalid input and missing calibration");
    auto invalid=dataset;invalid.samples[0].concentration=std::numeric_limits<double>::quiet_NaN();check(!model->fit(invalid).success,"reject invalid concentration");
    invalid=dataset;for(auto& s:invalid.samples)s.feature.peak=2;check(!model->fit(invalid).success,"reject singular matrix");
    invalid=dataset;invalid.samples[0].feature.channel=0x30;check(!model->fit(invalid).success,"reject mixed channel fitting");
    invalid=dataset;invalid.unit="ppm";check(!model->fit(invalid).success,"reject ambiguous unit");
    invalid=dataset;invalid.samples+=dataset.samples.first();check(!model->fit(invalid).success,"reject repeated source samples");
    QString error;Store store(temp.filePath("calibration/library.json"));Library library;
    check(store.load(library,&error),"load missing library as empty");dataset.revision=1;library.datasets.append(dataset);
    fitted=model->fit(dataset);library.fits.append(fitted.fit);library.active[816]=fitted.fit.id;
    check(store.save(library,&error),qPrintable(error));
    QProcess freshApplication;freshApplication.start(QCoreApplication::applicationFilePath(),{"--reload-library",store.filename(),fitted.fit.id});
    check(freshApplication.waitForFinished(10000)&&freshApplication.exitStatus()==QProcess::NormalExit&&freshApplication.exitCode()==0,"independent application process reloads saved parameters");
    Library reopened;check(Store(store.filename()).load(reopened,&error)&&reopened.activeFit(816)&&!reopened.activeFit(1368),"fresh store reload and per-wavelength active versions");
    check(calculate(dataset.samples.first(),816,reopened.activeFit(816)).value==5,"saved coefficients applied after reopen");
    const auto oldId=fitted.fit.id;dataset.revision=2;dataset.name="edited standard set";library.datasets.append(dataset);
    auto v2=model->fit(dataset);library.fits.append(v2.fit);library.active[816]=v2.fit.id;check(store.save(library,&error),qPrintable(error));
    library.active[816]=oldId;check(store.save(library,&error),"switch back to prior immutable calibration");
    check(!store.save(reopened,&error)&&error.contains(QStringLiteral("其他窗口")),"stale writer cannot lose another revision");
    auto tampered=library;tampered.fits[0].a=99;check(!store.save(tampered,&error),"reject history overwrite");
    auto wrongWave=library;wrongWave.active[1368]=oldId;check(!store.save(wrongWave,&error),"persisted cross-wavelength activation rejected");
    library.active[816].clear();check(store.save(library,&error),"disable calibration version");
    check(store.load(reopened,&error)&&!reopened.activeFit(816)&&reopened.fits.size()==2,"disable survives restart, versions retained");
    auto infrared=Dataset::create("1368 SOFTWARE TEST ONLY",1368,0x30,"ppmv");
    infrared.samples=importSession(first,infrared,10,true).samples;
    check(infrared.samples.first().feature.channel==0x30,"1368 imports the confirmed DLIA0 feature records");
    infrared.samples+=importSession(second,infrared,16,true).samples;infrared.revision=1;
    const auto irFit=model->fit(infrared);check(irFit.success&&irFit.fit.a==3&&irFit.fit.b==4,"1368 model fitted independently");
    library.datasets.append(infrared);library.fits.append(irFit.fit);library.active[1368]=irFit.fit.id;library.active[816]=oldId;
    check(store.save(library,&error)&&store.load(reopened,&error),"persist independent active versions for both wavelengths");
    check(calculate(dataset.samples.first(),816,reopened.activeFit(816)).value==5&&
        calculate(infrared.samples.first(),1368,reopened.activeFit(1368)).value==10,"both wavelengths use their own coefficients and data");
    auto json=v2.fit.toJson();json["model_version"]=99;Fit::fromJson(json,&error);check(!error.isEmpty(),"reject unsupported model version");
    error.clear();json=v2.fit.toJson();json["a"]=123;Fit::fromJson(json,&error);check(!error.isEmpty(),"validate saved coefficients against dataset snapshot");
    check(!importSession(temp.filePath("gone"),dataset,1,true).success,"missing source Session is explicit");
    check(exportResults(temp.filePath("results.json"),{concentration},&error,{predictionFit}),"export nested provenance atomically");
    const auto result=QJsonDocument::fromJson(read(temp.filePath("results.json"))).object()["results"].toArray().first().toObject();
    check(result["origin"]=="software_test"&&!result["compensated"].toBool()&&result["input"].toObject()["feature"].toObject()["id"]==concentration.input.feature.id,"export is traceable and never calls simulated concentration real");
    const auto metadata=SessionLoader::loadMetadata(first);auto catalog=SessionWaveformRepository::loadCatalog(metadata.metadata);
    check(catalog.success,"existing Session history catalog loads");const auto peaks=SessionWaveformRepository::loadCachedPeakSeries(catalog.catalog);
    check(peaks.success&&peaks.peakValues.size()==4&&peaks.peakValues[0]==2&&std::isnan(peaks.peakValues[2]),"viewer shares feature reader with quality gaps");
    check(SessionWaveformRepository::readFrame(catalog.catalog,0).success,"existing playback frame reads");
    check(read(first+"/raw/waveform.dat")==rawBefore&&read(first+"/sensors/waveform_features.csv")==featuresBefore,"import/fit/export do not alter Session files");
    // Malformed actual-format input must fail, not create default-valued records.
    QFile malformed(temp.filePath("bad.csv"));check(malformed.open(QIODevice::WriteOnly),"write corrupt fixture");malformed.write("host_time_us,peak\n1,2\n");malformed.close();
    check(!SessionWaveformFeatures::readFile(malformed.fileName(),temp.path()).success,"required CSV field validation");
    const auto allFeatures=SessionWaveformFeatures::read(first);check(allFeatures.success&&allFeatures.records.size()==8,"both channel row count matches actual file");
    const auto firstRow=featuresBefore.split('\n').at(1);
    check(malformed.open(QIODevice::WriteOnly|QIODevice::Truncate),"write duplicate CSV fixture");malformed.write(featuresBefore+firstRow+'\n');malformed.close();
    check(!SessionWaveformFeatures::readFile(malformed.fileName(),first).success,"duplicate rows within feature file rejected");
    check(malformed.open(QIODevice::WriteOnly|QIODevice::Truncate),"write closed CSV without final newline");malformed.write(featuresBefore.trimmed());malformed.close();
    check(SessionWaveformFeatures::readFile(malformed.fileName(),first).records.size()==8,"closed CSV final row retained without newline");
    std::cout<<"water_calibration_test passed (SOFTWARE TEST DATA ONLY)\n";return 0;
}
