#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "ground/session/GroundRecordingService.h"
#include "ground/calibration/RecordedConcentrations.h"
#include "SkySessionRecorder.h"
#include "shared/session/CalibrationRuntime.h"
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
using namespace IndependentCalibrationTest;
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);Rig r;auto f=r.complete();
    Ground::Session::GroundRecordingService recorder;recorder.setCalibrationPolicy(r.service);
    Ground::Session::GroundRecordingOptions options;options.baseDirectory=QDir(r.root.path()).filePath("sessions");
    QString error;check(!recorder.start(options,nullptr,&error),"formal Session requires selected valid vcal");
    r.bus.setDevice(false,{});r.service->synchronize();r.service->selectFile(f.filename);r.service->synchronize();
    check(r.service->view().selected.valid,"selection allowed before connection");
    check(!recorder.start(options,nullptr,&error),"connection required");
    r.bus.setDevice(true,r.identity);r.service->synchronize();
    check(recorder.start(options,nullptr,&error),qPrintable(error));const auto dir=recorder.status().sessionDirectory;
    check(r.service->view().locked,"formal start locks calibration in the service");
    r.service->selectFile(f.filename);r.service->synchronize();check(r.service->view().error,"selection blocked while recording");
    r.service->startStage(0);r.service->synchronize();check(!r.service->view().acquiring,"ordinary Session blocks independent acquisition");
    auto stream=cycle(816,4,888);check(recorder.recordFpgaHarmonics(1700000000000000ULL,stream),"shared canonical feature recorded");
    check(recorder.pause()&&r.service->view().locked,"pause retains calibration lock");
    check(recorder.start(options,nullptr,&error)&&r.service->view().locked,"resume uses original locked parameters");
    check(!recorder.stop().writeFailed&&!r.service->view().locked,"stop drains all workers before releasing lock");
    const auto rows=read(QDir(dir).filePath("calibration_concentrations.jsonl"));
    const auto row=QJsonDocument::fromJson(rows.trimmed()).object();
    check(row["valid"].toBool()&&row["initial_concentration"].toDouble()==200&&row["vcal_sha256"]==f.sha256,"recorded initial concentration traces vcal and parameters");
    check(!row["compensated"].toBool()&&row["compensation_status"].toString().contains(QStringLiteral("未进行")),"uncompensated state explicit");
    auto historical=QJsonDocument::fromJson(read(QDir(dir).filePath("calibration_snapshot.json"))).object();
    check(QFile::remove(f.filename),"original file can be removed after Session stop");
    auto original=CalibrationFileStore::decode(QJsonDocument(historical["vcal_document"].toObject()).toJson(QJsonDocument::Compact),false,&error);
    check(original.valid&&original.project.fits[816].parameters.a==50,"historical Session remains self-contained without original file");
    const auto history=readRecordedConcentrations(QDir(dir).filePath("calibration_concentrations.jsonl"));
    check(history.success&&history.rows.size()==1,qPrintable(history.error));
    const auto exportFile=QDir(r.root.path()).filePath("historical-export.json");
    check(exportRecordedConcentrations(exportFile,history,&error)&&readRecordedConcentrations(exportFile).success,"historical export is self-contained and revalidates original parameters");
    auto damaged=history;auto altered=damaged.rows[0].toObject();altered["initial_concentration"]=999;damaged.rows[0]=altered;
    check(!exportRecordedConcentrations(exportFile,damaged,&error),"historical concentration tampering rejected");
    r.service->selectFile(QStringLiteral("missing.vcal"));r.service->synchronize();
    check(QJsonDocument::fromJson(read(QDir(dir).filePath("calibration_snapshot.json"))).object()==historical,"new selections cannot overwrite history");
    {
        Rig active;active.task();active.service->addStage(100,5000);active.service->synchronize();active.pair(2,4);
        active.service->startStage(0);active.service->synchronize();Ground::Session::GroundRecordingService blocked;blocked.setCalibrationPolicy(active.service);
        check(!blocked.start(options,nullptr,&error),"independent acquisition prevents formal Session start");
        check(active.service->view().acquiring,"failed formal start does not alter independent task");
    }
    QTemporaryDir crashRoot;
    {
        Rig sky;const auto file=sky.complete();sky.service->selectFile(file.filename);sky.service->synchronize();
        QJsonObject snapshot;check(sky.service->prepare(snapshot,&error),"remote parameter snapshot prepared");snapshot.remove("vcal_document");
        {
            auto invalid=snapshot;auto runtime=invalid["runtime"].toObject();runtime["hardware_mapping"]=QJsonObject{{"816",0x30},{"1368",0x31}};
            runtime.remove("sha256");runtime["sha256"]=QString::fromLatin1(VaporView::Session::runtimeCalibrationDigest(runtime));invalid["runtime"]=runtime;
            SkySessionRecorder rejected;check(!rejected.setCalibrationSnapshot(invalid,&error),"remote mapping rejected despite recomputed digest");
            invalid=snapshot;invalid["file_id"]=calibrationUuid();check(!rejected.setCalibrationSnapshot(invalid,&error),"outer Session snapshot cannot change parameter identity");
        }
        const auto base=QDir(sky.root.path()).filePath("sky_sessions");QString session;
        {
            SkySessionRecorder writer;check(writer.setCalibrationSnapshot(snapshot,&error),"Sky validates transmitted independent parameter snapshot");
            check(writer.start(base,"",0,&error),qPrintable(error));session=writer.sessionDirectory();
            auto s=cycle(1368,8,5);check(writer.recordFpgaHarmonics(quint64(QDateTime::currentMSecsSinceEpoch())*1000,s),"Sky concentration uses shared full-cycle feature");
            writer.pause();check(!writer.setCalibrationSnapshot({},&error),"Sky pause prevents parameter switch");
            // Recorder destruction without stop models loss of the runtime;
            // producers are joined while the persistent lock remains.
        }
        SkySessionRecorder restarted;restarted.restoreCalibrationLock(base);
        check(restarted.hasRecoveredCalibrationLock()&&restarted.isPaused(),"Sky restart restores abnormal Session lock and paused status");
        check(!restarted.start(base,"",0,&error),"Sky cannot silently resume a lost writer interval");
        const auto manifestBefore=read(QDir(session).filePath("session.json"));restarted.pause();
        check(read(QDir(session).filePath("session.json"))==manifestBefore,"pausing recovered Sky state never overwrites historical counters");
        check(restarted.stop(&error)&&!restarted.hasRecoveredCalibrationLock(),"explicit Sky stop terminates recovered Session");
        const auto recorded=readRecordedConcentrations(QDir(session).filePath("calibration_snapshot.json"));
        check(recorded.success&&recorded.rows.size()==1&&recorded.rows[0].toObject()["initial_concentration"].toDouble()==200,"headless Sky history independently reproduces 1368 nm concentration");
        check(!restarted.hasCalibrationSnapshot(),"a new Sky Session requires fresh parameter selection");
    }
    {
        Rig source;const auto file=source.complete();CalibrationFileStore target(crashRoot.path());CalibrationFile copy;
        check(target.copy(file.filename,"crash fixture",copy,&error),"copy software fixture to second installation");
        auto service=std::make_shared<CalibrationService>(crashRoot.path());service->setDevice(true,source.identity);service->selectFile(copy.filename);service->synchronize();
        QJsonObject snapshot;check(service->prepare(snapshot,&error),"persist simulated abrupt-exit lock");
    }
    {
        CalibrationService recovered(crashRoot.path());recovered.synchronize();check(recovered.view().locked&&recovered.view().selected.valid,"restart restores parameter snapshot and lock");
        recovered.selectFile(QStringLiteral("anything"));recovered.synchronize();check(recovered.view().locked,"UI restart never unlocks Session");
        recovered.finishRecoveredSession();recovered.synchronize();check(!recovered.view().locked,"explicit recovered Session termination releases lock");
    }
    std::cout<<"Independent acquisition / vcal / Session lock software E2E passed.\n";
}
