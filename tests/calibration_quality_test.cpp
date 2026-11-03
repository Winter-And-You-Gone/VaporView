#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "ground/session/GroundRecordingService.h"
#include "ground/calibration/RecordedConcentrations.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLockFile>
#include <QProcess>
#include <QScopeGuard>
#include <QThread>
#include <QDirIterator>
#ifdef Q_OS_WIN
#include <psapi.h>
#include <aclapi.h>
#endif

using namespace IndependentCalibrationTest;
namespace {
quint64 peakMemoryBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        return memory.PeakWorkingSetSize;
#endif
    return 0;
}
void report(QJsonObject fields)
{
    fields["evidence"] = "software_simulation_only";
    fields["peak_working_set_bytes"] = QString::number(peakMemoryBytes());
    std::cout << QJsonDocument(fields).toJson(QJsonDocument::Compact).constData() << std::endl;
}
CalibrationProject syntheticProject(int stages, int rate)
{
    auto project = CalibrationProject::create(QStringLiteral("软件模拟容量测试：完整元数据与审核记录"), "ppmv");
    project.origin = "software_test";
    project.deviceIdentity = {{"backend", "simulator"}, {"serial", "SOFTWARE_CAPACITY_TEST_ONLY"}, {"transport", "local_fpga"}};
    const QString stream = calibrationUuid();
    quint64 sequence = 0;
    for (int stageIndex = 0; stageIndex < stages; ++stageIndex) {
        auto stage = ConcentrationStage::create(100.0 * (stageIndex + 1));
        stage.effectiveMs = stage.wallMs = stage.configuredMs;
        stage.status = "complete";
        stage.startedUtc = stage.completedUtc = calibrationUtcNow();
        stage.note = QStringLiteral("软件模拟标气阶段，不代表实机精度或吞吐");
        for (int wavelength : {816, 1368}) {
            auto& rows = stage.samples[wavelength];
            rows.reserve(600 * rate);
            for (int index = 0; index < 600 * rate; ++index) {
                PeakRecord record;
                record.taskId = project.id;
                record.stageId = stage.id;
                record.deviceIdentity = project.deviceIdentity;
                auto& feature = record.feature;
                ++sequence;
                feature.streamIdentity = stream;
                feature.hostTimeUs = 1700000000000000ULL + quint64(stageIndex * 600000 + index * 1000 / rate) * 1000;
                feature.waveformSequence = sequence;
                feature.segmentId = 1;
                feature.channel = channelForWavelength(wavelength);
                feature.id = QString::fromLatin1(QCryptographicHash::hash(
                    (stream + '|' + QString::number(feature.channel) + '|' + QString::number(sequence)).toUtf8(),
                    QCryptographicHash::Sha256).toHex());
                feature.cycleId = quint32(sequence);
                feature.fpgaTick = sequence * 100000;
                feature.originalPoints = feature.validPoints = 1024;
                feature.peakIndex = 512;
                feature.peak = (stageIndex + 1) * (wavelength == 816 ? 2.0 : 4.0) + (index % 11 - 5) * 0.001;
                record.environment.temperatureC = 25.123;
                record.environment.pressureHpa = 1013.25;
                record.environment.source = "SOFTWARE_TEST_PTB_HMP";
                record.environment.snapshotTimeUs = feature.hostTimeUs;
                record.alignment = "host_receipt_uncertain";
                for (const QString& source : {QStringLiteral("ptb"), QStringLiteral("hmp")})
                    record.sensorReadings[source] = QJsonObject{{"valid", true}, {"source", source},
                        {"host_received_utc_us", QString::number(feature.hostTimeUs)}, {"peak_delta_us", "0"},
                        {"temperature_c", 25.123}, {"pressure_hpa", 1013.25}, {"timestamp_kind", "host_receipt_uncertain"}};
                rows.append(record);
                if (index % 1000 == 0)
                    stage.operations.append({feature.id, QStringLiteral("软件测试：人工排除保留原始值"), calibrationUtcNow(), true, project.revision});
            }
        }
        project.stages.append(stage);
    }
    if (stages > 1) check(project.fit().isEmpty(), "capacity task independently fits both wavelengths");
    const auto error = project.validate(stages > 1);
    check(error.isEmpty(), qPrintable(error));
    return project;
}
void capacity(int stages, int rate)
{
    QTemporaryDir root;
    check(root.isValid(), "capacity installation root");
    QElapsedTimer clock;
    clock.start();
    auto project = syntheticProject(stages, rate);
    const auto createMs = clock.restart();
    quint64 used = 0;
    for (const auto& stage : project.stages)
        for (int wavelength : {816, 1368}) used += stage.statistics(wavelength).used;
    const auto statisticsMs = clock.restart();
    if (stages > 1) project.fit();
    const auto fitMs = clock.restart();
    qsizetype projectBytes = 0;
    {
        auto object = project.toJson();
        const auto jsonMs = clock.restart();
        auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        projectBytes = bytes.size();
        report({{"phase", "capacity_json"}, {"stages", stages}, {"cycles_per_lane_per_second", rate},
            {"records", stages * rate * 1200}, {"project_bytes", QString::number(projectBytes)},
            {"create_ms", createMs}, {"statistics_ms", statisticsMs}, {"fit_ms", fitMs},
            {"json_object_ms", jsonMs}, {"serialize_ms", clock.elapsed()}, {"used", QString::number(used)}});
    }
    clock.restart();
    QString error;
    CalibrationFileStore store(root.path());
    const bool saved = store.saveDraft(project, &error);
    const auto saveMs = clock.restart();
    if (!saved) {
        report({{"phase", "capacity_save_rejected"}, {"stages", stages}, {"save_ms", saveMs}, {"error", error}});
        check(projectBytes > 256LL * 1024 * 1024, "only oversized baseline fixture is rejected");
        return;
    }
    const auto filename = store.drafts().first();
    const auto bytes = read(filename);
    const auto readMs = clock.restart();
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    const auto hashMs = clock.restart();
    {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        check(parse.error == QJsonParseError::NoError && document.isObject(), "large JSON parses");
    }
    const auto parseMs = clock.restart();
    auto restored = store.read(filename, true, &error);
    const auto loadMs = clock.restart();
    check(restored.valid, qPrintable(error));
    check(restored.project.stages.size() == stages && digest.size() == 32, "capacity draft reloads without stage loss");
    quint64 restoredUsed = 0;
    for (const auto& stage : restored.project.stages)
        for (int wavelength : {816, 1368}) restoredUsed += stage.statistics(wavelength).used;
    check(restoredUsed == used, "all raw statistics survive capacity roundtrip");
    report({{"phase", "capacity_roundtrip"}, {"stages", stages}, {"file_bytes", QString::number(bytes.size())},
        {"save_ms", saveMs}, {"read_ms", readMs}, {"hash_ms", hashMs}, {"parse_ms", parseMs},
        {"load_validate_refit_ms", loadMs}, {"restored_statistics_ms", clock.elapsed()}});
    restored={};
    if(projectBytes>=240LL*1024*1024) {
        clock.restart();CalibrationService service(root.path(),[]{return quint64(10000);});
        service.setDevice(true,project.deviceIdentity);service.openProject(filename,true);service.synchronize();
        const auto original=service.view();service.startStage(0);service.synchronize();const auto guarded=service.view();
        check(!guarded.acquiring && guarded.error && guarded.message.contains(QStringLiteral("容量")),
            "near-limit task is rejected before acquiring another raw cycle");
        check(guarded.project.stages.first().samples[816].size()==original.project.stages.first().samples[816].size(),
            "capacity guard retains all existing raw records without truncation");
        report({{"phase","capacity_guard"},{"guard_ms",clock.elapsed()},{"service_diagnostics",service.diagnostics()}});
    }
    if(stages>1) {
        clock.restart();CalibrationFile generated;
        check(store.generate(project,QStringLiteral("软件容量测试完整定标"),generated,&error),qPrintable(error));
        quint64 generatedUsed=0;
        for(const auto& stage:generated.project.stages)for(int wave:{816,1368})generatedUsed+=stage.statistics(wave).used;
        check(generatedUsed==used,"generated vcal retains every raw input, exclusion and independent fit");
        report({{"phase","capacity_final_vcal"},{"stages",stages},{"generate_validate_reload_ms",clock.elapsed()},
            {"file_bytes",QString::number(QFileInfo(generated.filename).size())},{"raw_statistics_used",QString::number(generatedUsed)}});
    }
}
void throughput(int count)
{
    Rig rig;
    rig.task();
    rig.service->addStage(100, 600000);
    rig.service->synchronize();
    rig.pair(2, 4);
    rig.service->startStage(0);
    rig.service->synchronize();
    QElapsedTimer timer;
    timer.start();
    qint64 maximumPublishNs = 0;
    for (int i = 0; i < count; ++i) {
        const auto before = timer.nsecsElapsed();
        rig.now = 10000 + quint64(i / 100);
        rig.bus.publish(1700000000000000ULL + rig.now * 1000,
            cycle(i % 2 ? 1368 : 816, i % 2 ? 4 : 2, rig.cycleId++));
        maximumPublishNs = std::max(maximumPublishNs, timer.nsecsElapsed() - before);
    }
    const auto offerMs = timer.elapsed();
    rig.service->synchronize();
    const auto elapsedMs = timer.elapsed();
    const auto view = rig.service->view();
    int records = 0;
    for (const auto& lane : view.project.stages.first().samples) records += lane.size();
    const auto diagnostics=rig.service->diagnostics();
    const auto number=[&](const char *key){return diagnostics[key].toString().toULongLong();};
    check(number("processed")+number("dropped")==number("input"),"burst input is drained or explicitly rejected");
    check(number("mailbox_peak")<=4096 && diagnostics["mailbox_current"].toInt()==0,"burst mailbox remains bounded and drains");
    check(number("recorded")==quint64(records),"burst counters match every retained raw record");
    if(number("dropped"))check(!view.acquiring && view.project.stages.first().status!="complete" && view.error,
        "overflow pauses visibly without false completion");
    check(number("abandoned_after_admission")==0,"overflow retains all admitted raw peaks for the active stage");
    report({{"phase", "throughput"}, {"offered", count}, {"recorded", records}, {"offer_ms", offerMs},
        {"drain_save_ms", elapsedMs}, {"maximum_publish_ns", maximumPublishNs}, {"acquiring", view.acquiring},
        {"message", view.message}, {"service_diagnostics", diagnostics},{"bus_diagnostics",rig.bus.diagnostics()}});
}
quint64 counter(const QJsonObject& j,const char *key){return j[key].toString().toULongLong();}
void start(Rig& rig,quint64 duration=600000)
{
    rig.task();rig.service->addStage(100,duration);rig.service->synchronize();rig.pair(2,4);
    rig.service->startStage(0);rig.service->synchronize();check(rig.service->view().acquiring,"software stream starts");
}
void featureContract()
{
    const auto base=syntheticProject(1,1).stages.first().samples[816].first().feature;
    QVector<Ground::HarmonicPeakFeature> features{base};
    const auto add=[&](auto mutate){auto f=base;mutate(f);features.append(f);};
    add([](auto& f){f.id="wrong";});add([](auto& f){f.hostTimeUs=0;});
    add([](auto& f){f.channel=0x32;});add([](auto& f){f.peakIndex=-2;});
    add([](auto& f){f.peakIndex=UINT64_C(0x100000000);});
    add([](auto& f){f.validPoints=f.originalPoints+1;});add([](auto& f){f.originalPoints=0;});
    add([](auto& f){f.peakIndex=f.originalPoints;});
    add([](auto& f){f.peak=std::numeric_limits<double>::infinity();});
    add([](auto& f){f.peak=std::numeric_limits<double>::quiet_NaN();});
    add([](auto& f){f.qualityFlags=UINT32_MAX;});
    for(double peak:{0.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        for(quint32 flags:{0U,2U})add([&](auto& f){f.validPoints=0;f.peakIndex=-1;f.peak=peak;f.qualityFlags=flags;});
    for(const auto& feature:features) {
        QString error;const auto decoded=Ground::HarmonicPeakFeature::fromJson(feature.toJson(),&error);
        const bool canonical=error.isEmpty() && (decoded.peak==feature.peak || (std::isnan(decoded.peak)&&std::isnan(feature.peak)));
        check(feature.sourceError().isEmpty()==canonical,"native fast validation preserves the serialized source contract");
    }
}
void rates()
{
    for(int rate:{1,10,100,1000}) {
        Rig rig;const quint64 duration=rate==1?600000:10000;start(rig,duration);
        QElapsedTimer timer;timer.start();qint64 maximumPublishNs=0;
        const int cycles=int(duration)*rate/1000;
        for(int i=1;i<=cycles;++i) {
            rig.now=10000+quint64(i)*1000/rate;
            for(int wave:{816,1368}) {
                const auto before=timer.nsecsElapsed();
                rig.bus.publish(1700000000000000ULL+rig.now*1000,cycle(wave,wave==816?2:4,rig.cycleId++));
                maximumPublishNs=std::max(maximumPublishNs,timer.nsecsElapsed()-before);
            }
            if(i%std::max(1,rate/10)==0)rig.service->synchronize();
        }
        rig.service->synchronize();const auto view=rig.service->view();const auto& stage=view.project.stages.first();
        const auto diagnostics=rig.service->diagnostics();
        check(stage.status=="complete" && stage.effectiveMs==duration,"paced software input completes only the common valid window");
        check(stage.samples[816].size()==cycles && stage.samples[1368].size()==cycles,"paced rate retains every raw record on both lanes");
        check(!counter(diagnostics,"dropped") && !counter(diagnostics,"invalid"),"paced supported software rates lose no records");
        report({{"phase","paced_rate"},{"cycles_per_lane_per_second",rate},{"virtual_duration_ms",QString::number(duration)},
            {"wall_ms",timer.elapsed()},{"maximum_publish_ns",maximumPublishNs},{"service_diagnostics",diagnostics}});
        if(rate==1) {
            const auto baselineRaw=counter(diagnostics,"recorded");
            timer.restart();
            for(int second=1;second<=43200;++second) {
                rig.now+=1000;
                for(int wave:{816,1368})rig.bus.publish(1700000000000000ULL+rig.now*1000,
                    cycle(wave,wave==816?2:4,rig.cycleId++));
                if(second%100==0)rig.service->synchronize();
            }
            rig.service->synchronize();const auto after=rig.service->diagnostics();
            check(counter(after,"recorded")==baselineRaw,"twelve-hour simulated live stream cannot grow a completed task's raw storage");
            check(rig.bus.diagnostics()["deduplication_current"].toInt()<=4096 && counter(after,"mailbox_peak")<=4096,
                "long-run live cache and mailbox remain bounded");
            report({{"phase","long_live_after_completion"},{"virtual_hours",12},{"wall_ms",timer.elapsed()},
                {"service_diagnostics",after},{"bus_diagnostics",rig.bus.diagnostics()}});
        }
    }
}
void interruptions()
{
    Rig rig;start(rig);rig.now+=500;rig.pair(2,4);
    const auto common=rig.service->view().project.stages.first().effectiveMs;
    for(int i=0;i<5;++i) {
        rig.now+=500;rig.bus.publish(1700000000000000ULL+rig.now*1000,cycle(816,2,rig.cycleId++));rig.service->synchronize();
    }
    auto view=rig.service->view();
    check(!view.acquiring && view.project.stages.first().effectiveMs==common,"missing 1368 lane freezes common time and then pauses");
    rig.pair(2,4);rig.service->startStage(0);rig.service->synchronize();rig.now+=1000;rig.pair(2,4);
    check(rig.service->view().project.stages.first().effectiveMs==common+1000,"short healthy gap resumes the remaining common window");
    rig.now+=3000;rig.pair(2,4);view=rig.service->view();
    check(!view.acquiring && view.project.stages.first().effectiveMs==common+1000,"long gap cannot be credited by later backfill");
    rig.pair(2,4);rig.service->startStage(0);rig.service->synchronize();
    auto remote=cycle(816,2,rig.cycleId++);remote.featureStreamId=calibrationUuid();remote.featureSequence=10;
    rig.bus.publish(1700000000000000ULL+rig.now*1000,remote);rig.service->synchronize();
    rig.bus.publish(1700000000000000ULL+rig.now*1000,remote); // Exact transport replay is suppressed by the bus.
    remote.cycleId=rig.cycleId++;remote.featureSequence=9;
    rig.bus.publish(1700000000000000ULL+rig.now*1000,remote);rig.service->synchronize();view=rig.service->view();
    check(!view.acquiring && view.project.stages.first().statistics(816).invalid==1,"remote out-of-order sequence is retained as invalid and pauses");
    check(counter(rig.bus.diagnostics(),"duplicate")==1 && counter(rig.bus.diagnostics(),"continuity_errors")==1,
        "bus reports transport replay and sequence discontinuity independently");
    report({{"phase","interruptions_and_order"},{"service_diagnostics",rig.service->diagnostics()},{"bus_diagnostics",rig.bus.diagnostics()}});
}
void failuresAndShutdown()
{
    {
        Rig rig;start(rig);rig.now+=500;rig.pair(2,4);const auto before=rig.service->view();
        QLockFile held(QDir(CalibrationFileStore(rig.root.path()).directory()).filePath(".write.lock"));
        check(held.tryLock(0),"hold draft writer lock");rig.now+=1000;
        rig.bus.publish(1700000000000000ULL+rig.now*1000,cycle(816,2,rig.cycleId++));rig.service->synchronize();
        const auto failed=rig.service->view();
        check(!failed.acquiring && failed.error && !failed.message.contains(QStringLiteral("已保存")),"write failure cannot claim success or continue acquiring");
        check(counter(rig.service->diagnostics(),"save_failures")>0 && failed.project.stages.first().samples[816].size()>=before.project.stages.first().samples[816].size(),
            "save failure preserves complete in-memory raw prefix and reports diagnostics");
        held.unlock();rig.service->synchronize();QString error;const auto files=CalibrationFileStore(rig.root.path()).drafts();
        check(CalibrationFileStore(rig.root.path()).read(files.first(),true,&error).valid,qPrintable(error));
    }
    {
        Rig rig;start(rig);
        for(int i=0;i<2000;++i)rig.bus.publish(1700000000000000ULL+rig.now*1000,cycle(i%2?1368:816,i%2?4:2,rig.cycleId++));
        rig.service.reset();QString error;CalibrationFileStore store(rig.root.path());const auto saved=store.read(store.drafts().first(),true,&error);
        check(saved.valid,qPrintable(error));const auto& stage=saved.project.stages.first();
        check(stage.status=="paused" && stage.samples[816].size()+stage.samples[1368].size()==2000,
            "service destruction drains admitted peaks and checkpoints a paused stage");
    }
}
void overloadedCheckpoint()
{
    Rig rig;auto project=syntheticProject(2,10);project.deviceIdentity=rig.identity;
    for(auto& stage:project.stages)for(auto& lane:stage.samples)for(auto& row:lane)row.deviceIdentity=rig.identity;
    project.stages.append(ConcentrationStage::create(300));project.invalidateFits();
    CalibrationFileStore store(rig.root.path());QString error;
    check(store.saveDraft(project,&error),qPrintable(error));
    rig.service->openProject(store.drafts().first(),true);rig.service->synchronize();rig.pair(2,4);
    rig.service->startStage(2);
    std::thread producer([&]{
        QElapsedTimer deadline;deadline.start();
        while(!rig.service->view().acquiring && deadline.elapsed()<30000)QThread::msleep(1);
        check(rig.service->view().acquiring,"overload producer observes real full checkpoint in progress");
        for(int i=0;i<20000;++i)rig.bus.publish(1700000000000000ULL+rig.now*1000,
            cycle(i%2?1368:816,i%2?4:2,rig.cycleId++));
    });
    producer.join();rig.service->synchronize();const auto view=rig.service->view();const auto diagnostics=rig.service->diagnostics();
    check(counter(diagnostics,"dropped")>0 && !view.acquiring && view.error && view.project.stages.last().status=="paused",
        "sustained input during full checkpoint reaches bounded overflow and explicitly pauses");
    check(counter(diagnostics,"mailbox_peak")==4096 && !counter(diagnostics,"abandoned_after_admission"),
        "all admitted peaks survive overload rather than disappearing after pause");
    check(counter(diagnostics,"processed")+counter(diagnostics,"dropped")==counter(diagnostics,"input"),
        "overload accounts for all input cycles");
    check(!view.project.stages.last().effectiveMs,"overflow cannot invent a complete common interval");
    report({{"phase","overloaded_checkpoint"},{"service_diagnostics",diagnostics},{"bus_diagnostics",rig.bus.diagnostics()}});
}
void faultChild(const QString& root,const QString& phase)
{
    CalibrationFileStore store(root);QString error;const auto filename=store.drafts().first();
    auto saved=store.read(filename,true,&error);check(saved.valid,qPrintable(error));
    if(phase=="save") {
        auto project=syntheticProject(10,10);project.id=saved.project.id;
        for(auto& stage:project.stages)for(auto& lane:stage.samples)for(auto& row:lane)row.taskId=project.id;
        std::cout<<"SAVING"<<std::endl;
        check(store.saveDraft(project,&error),qPrintable(error));std::cout<<"SAVED"<<std::endl;return;
    }
    std::atomic<quint64> now{10000};CalibrationService service(root,[&]{return now.load();});RealtimePeakBus bus;
    QObject::connect(&bus,&RealtimePeakBus::peakReady,&service,&CalibrationService::offer,Qt::DirectConnection);
    QObject::connect(&bus,&RealtimePeakBus::deviceChanged,&service,&CalibrationService::setDevice,Qt::DirectConnection);
    bus.setDevice(true,saved.project.deviceIdentity);service.openProject(filename,true);service.synchronize();quint32 id=1;
    const auto pair=[&]{for(int wave:{816,1368})bus.publish(1700000000000000ULL+now*1000,cycle(wave,wave==816?2:4,id++));service.synchronize();};
    pair();service.startStage(0);service.synchronize();check(service.view().acquiring,"fault child starts isolated acquisition");
    for(;;) {
        now+=100;pair();const auto stage=service.view().project.stages.first();
        std::cout<<"COMMITTED "<<stage.samples[816].size()<<' '<<stage.effectiveMs<<std::endl;
        QThread::msleep(50);
    }
}
QByteArray awaitChildLine(QProcess& child,const QByteArray& prefix)
{
    QElapsedTimer timer;timer.start();QByteArray output;
    while(timer.elapsed()<30000 && child.state()!=QProcess::NotRunning) {
        child.waitForReadyRead(50);output+=child.readAllStandardOutput();
        for(const auto& line:output.split('\n'))if(line.startsWith(prefix))return line.trimmed();
    }
    std::cerr<<output.constData()<<child.readAllStandardError().constData();check(false,"fault child reaches observable phase");return {};
}
void processFaults()
{
    QTemporaryDir root(QDir::tempPath()+QStringLiteral("/VaporView_软件恢复_XXXXXX"));check(root.isValid(),"Chinese fault installation root");
    auto project=syntheticProject(1,1);auto& stage=project.stages.first();stage.status="paused";stage.effectiveMs=stage.wallMs=1000;
    CalibrationFileStore store(root.path());QString error;check(store.saveDraft(project,&error),qPrintable(error));
    const auto filename=store.drafts().first();
    QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--fault-child",root.path(),"acquire"});
    check(child.waitForStarted(5000),"launch owned acquisition child");
    const auto committed=awaitChildLine(child,"COMMITTED ").split(' ');const int count=committed[1].toInt();const quint64 effective=committed[2].toULongLong();
    const auto acquisitionPid=child.processId();child.kill();check(child.waitForFinished(5000),"terminate and reap owned acquisition child");
    auto restored=store.read(filename,true,&error);check(restored.valid,qPrintable(error));
    check(restored.project.stages.first().samples[816].size()>=count && restored.project.stages.first().effectiveMs>=effective,
        "forced acquisition termination recovers every acknowledged atomic save");
    {
        quint64 now=30000;CalibrationService recovery(root.path(),[&]{return now;});
        recovery.setDevice(true,project.deviceIdentity);recovery.openProject(filename,true);recovery.synchronize();
        const auto before=recovery.view().project.stages.first();
        check(before.status=="paused" && before.effectiveMs==restored.project.stages.first().effectiveMs,"restart never credits the offline interval");
        recovery.offer(before.samples[816].first());recovery.synchronize();
        check(counter(recovery.diagnostics(),"duplicate")==1,"forced recovery rejects a saved record ID replay");
        recovery.setDevice(true,{{"backend","simulator"},{"serial","DIFFERENT_TEST_DEVICE"}});recovery.startStage(0,true);recovery.synchronize();
        check(!recovery.view().acquiring && recovery.view().project.deviceIdentity==project.deviceIdentity,"identity confirmation cannot merge another device after a kill");
    }
    const auto checkpoint=read(filename);const auto checkpointHash=QCryptographicHash::hash(checkpoint,QCryptographicHash::Sha256);
    child.start(QCoreApplication::applicationFilePath(),{"--fault-child",root.path(),"save"});check(child.waitForStarted(5000),"launch owned saving child");
    awaitChildLine(child,"SAVING");const auto savingPid=child.processId();
    QElapsedTimer deadline;deadline.start();qint64 temporaryBytes=-1;
#ifdef Q_OS_WIN
    HANDLE temporary=INVALID_HANDLE_VALUE;
    const auto closeTemporary=qScopeGuard([&]{if(temporary!=INVALID_HANDLE_VALUE)CloseHandle(temporary);});
#endif
    while(deadline.elapsed()<30000 && child.state()!=QProcess::NotRunning) {
        const auto entries=QDir(store.draftsDirectory()).entryInfoList({QFileInfo(filename).fileName()+".*"},QDir::Files|QDir::Hidden);
        for(const auto& entry:entries) {
#ifdef Q_OS_WIN
            // Directory-entry sizes can remain zero until Windows closes the
            // writer. Query the live file handle instead. Denying delete sharing
            // holds the rename boundary while the parent observes and kills it.
            if(temporary==INVALID_HANDLE_VALUE)temporary=CreateFileW(
                reinterpret_cast<LPCWSTR>(entry.absoluteFilePath().utf16()),FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            LARGE_INTEGER size{};
            if(temporary!=INVALID_HANDLE_VALUE && GetFileSizeEx(temporary,&size))temporaryBytes=size.QuadPart;
#else
            temporaryBytes=entry.size();
#endif
            if(temporaryBytes>0)break;
        }
        if(temporaryBytes>0)break;child.waitForFinished(1);
    }
    check(temporaryBytes>0,"observe an actual QSaveFile temporary during the save attempt");
    child.kill();check(child.waitForFinished(5000),"terminate and reap owned child during atomic save attempt");
#ifdef Q_OS_WIN
    CloseHandle(temporary);temporary=INVALID_HANDLE_VALUE;
#endif
    restored=store.read(filename,true,&error);check(restored.valid,qPrintable(error));
    const bool oldCheckpoint=QCryptographicHash::hash(read(filename),QCryptographicHash::Sha256)==checkpointHash;
    check(oldCheckpoint || restored.project.stages.size()==10,"killed save recovers an intact old or fully committed new checkpoint");
    check(restored.project.validate().isEmpty(),"killed save never exposes partial records, altered statistics or duplicate IDs");
    report({{"phase","process_fault_recovery"},{"acquisition_child_pid",QString::number(acquisitionPid)},
        {"saving_child_pid",QString::number(savingPid)},{"observed_atomic_temporary_bytes",QString::number(temporaryBytes)},
        {"recovered_checkpoint",oldCheckpoint?"previous_commit":"new_commit"},{"acknowledged_effective_ms",QString::number(effective)}});
}
void journal()
{
    QTemporaryDir root;
    auto project=syntheticProject(3,1);
    project.invalidateFits();
    auto& active=project.stages.last();active.status="acquiring";active.effectiveMs=active.wallMs=1000;
    CalibrationFileStore store(root.path());QString error;
    check(store.saveDraft(project,&error),qPrintable(error));
    const auto filename=store.drafts().first();
    const auto checkpoint=read(filename);
    quint64 sequence=100000;
    QElapsedTimer timer;timer.start();
    for(int batch=0;batch<5;++batch) {
        for(int wave:{816,1368}) {
            auto record=active.samples[wave].last();auto& feature=record.feature;
            feature.waveformSequence=++sequence;feature.cycleId=quint32(sequence);feature.fpgaTick+=sequence;
            feature.hostTimeUs+=1000000;
            feature.id=QString::fromLatin1(QCryptographicHash::hash(
                (feature.streamIdentity+'|'+QString::number(feature.channel)+'|'+QString::number(sequence)).toUtf8(),
                QCryptographicHash::Sha256).toHex());
            active.samples[wave].append(record);
        }
        active.effectiveMs+=500;active.wallMs+=500;
        check(store.saveAcquisitionProgress(project,&error),qPrintable(error));
        check(read(filename)==checkpoint,"periodic progress does not rewrite the full checkpoint");
    }
    const auto incrementalMs=timer.restart();
    auto restored=store.read(filename,true,&error);
    check(restored.valid,qPrintable(error));
    check(restored.project.stages.last().samples[816].size()==605 &&
        restored.project.stages.last().effectiveMs==3500,"committed parts restore records and common effective time exactly once");
    check(restored.project.stages[0].operations.size()==project.stages[0].operations.size(),"incremental recovery retains review audit");
    const auto recoveryMs=timer.restart();
    const auto parts=QDir(filename+".parts").entryList({"*.vcalpart"},QDir::Files,QDir::Name);
    check(parts.size()==5,"one atomic part per successful progress save");
    const auto partPath=QDir(filename+".parts").filePath(parts[2]);const auto originalPart=read(partPath);
    auto corrupt=originalPart;corrupt[corrupt.size()/2]^=1;write(partPath,corrupt);
    check(!store.read(filename,true,&error).valid && !error.isEmpty(),"corrupted committed part fails closed and retains checkpoint");
    write(partPath,originalPart);
    const auto oldPartName=parts.first();const auto oldPart=read(QDir(filename+".parts").filePath(oldPartName));
    check(store.saveDraft(project,&error),qPrintable(error));
    check(!QDir(filename+".parts").exists(),"successful full checkpoint compacts obsolete journal parts");
    // A kill after atomic checkpoint commit can leave old-generation files.
    QDir().mkpath(filename+".parts");write(QDir(filename+".parts").filePath(oldPartName),oldPart);
    write(QDir(filename+".parts").filePath("interrupted.vcalpart.tmp"),"{partial");
    restored=store.read(filename,true,&error);
    check(restored.valid && restored.project.stages.last().samples[1368].size()==605,
        "obsolete checkpoint parts and uncommitted atomic temporaries cannot duplicate saved cycles");
    {
        QLockFile held(QDir(store.directory()).filePath(".write.lock"));
        check(held.tryLock(0),"simulate competing writer");
        check(!store.saveAcquisitionProgress(project,&error),"competing writer reports save failure");
    }
    check(store.saveAcquisitionProgress(project,&error),qPrintable(error));
    const auto validCheckpoint=read(filename);write(filename,validCheckpoint+"external change");
    check(!store.saveAcquisitionProgress(project,&error) && error.contains(QStringLiteral("外部修改")),
        "external checkpoint replacement cannot be silently overwritten by autosave");
    report({{"phase","draft_journal"},{"incremental_saves",5},{"incremental_ms",incrementalMs},
        {"recovery_ms",recoveryMs},{"checkpoint_bytes",QString::number(checkpoint.size())}});
}
void filesystemFaults()
{
#ifdef Q_OS_WIN
    QTemporaryDir root;auto project=syntheticProject(2,1);CalibrationFileStore store(root.path());QString error;
    check(store.saveDraft(project,&error),qPrintable(error));const auto path=store.drafts().first();const auto before=read(path);
    const HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"hold draft without delete sharing");
    const bool lockedSave=store.saveDraft(project,&error);CloseHandle(held);
    check(!lockedSave&&!error.isEmpty()&&read(path)==before,"real Windows sharing violation preserves the last committed draft");
    check(store.saveDraft(project,&error),qPrintable(error));

    HANDLE token=nullptr;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,&token),"open process identity");
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);QByteArray user(int(size),0);
    check(GetTokenInformation(token,TokenUser,user.data(),size,&size),"read current user SID");
    auto *sid=reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid;
    const auto directory=store.draftsDirectory();PSECURITY_DESCRIPTOR descriptor=nullptr;PACL original=nullptr,denied=nullptr;
    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(directory.utf16())),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original,nullptr,&descriptor)==ERROR_SUCCESS,"read isolated draft ACL");
    EXPLICIT_ACCESSW deny{};deny.grfAccessPermissions=FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY;deny.grfAccessMode=DENY_ACCESS;
    deny.Trustee.TrusteeForm=TRUSTEE_IS_SID;deny.Trustee.ptstrName=reinterpret_cast<LPWSTR>(sid);
    check(SetEntriesInAclW(1,&deny,original,&denied)==ERROR_SUCCESS,"construct isolated write-denied ACL");
    const auto aclResult=SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(directory.utf16())),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,denied,nullptr);
    check(aclResult==ERROR_SUCCESS,"deny creation of atomic draft temporaries");
    const auto committed=read(path);const bool deniedSave=store.saveDraft(project,&error);const auto deniedMessage=error;
    const auto restored=SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(directory.utf16())),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,original,nullptr);
    LocalFree(denied);LocalFree(descriptor);
    check(restored==ERROR_SUCCESS,"restore original isolated directory ACL");
    check(!deniedSave&&!deniedMessage.isEmpty()&&read(path)==committed,"actual ACL denial reports failure and retains every committed record");
    check(store.saveDraft(project,&error),qPrintable(error));

    BYTE adminStorage[SECURITY_MAX_SID_SIZE];DWORD adminSize=sizeof(adminStorage);
    check(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,adminStorage,&adminSize),"construct Administrators SID");
    SID_AND_ATTRIBUTES disabled{adminStorage,0};HANDLE restricted=nullptr;
    check(CreateRestrictedToken(token,DISABLE_MAX_PRIVILEGE,1,&disabled,0,nullptr,0,nullptr,&restricted),"create restricted ordinary-access test token");
    check(ImpersonateLoggedOnUser(restricted),"impersonate restricted token for actual calibration file IO");
    BOOL administrator=TRUE;const bool membership=CheckTokenMembership(nullptr,adminStorage,&administrator);
    CalibrationFile generated;const bool generatedOk=store.generate(project,QStringLiteral("受限令牌 中文定标文件"),generated,&error);
    const bool readOk=generatedOk&&store.read(generated.filename,false,&error).valid;
    const bool reverted=RevertToSelf();CloseHandle(restricted);CloseHandle(token);
    check(reverted&&membership&&!administrator,"restore caller after verifying disabled administrator membership");
    check(generatedOk&&readOk,qPrintable(error));
    report({{"phase","filesystem_faults"},{"sharing_violation",true},{"acl_write_denied",true},
        {"restricted_token_chinese_vcal_roundtrip",true},{"physical_disk_full","not_executed"}});
#endif
}
void deploymentFixture(const QString& root,bool verify)
{
    CalibrationFileStore store(root);const auto manifestPath=QDir(root).filePath("quality-user-data.json");QString error;
    if(!verify) {
        auto project=syntheticProject(2,1);CalibrationFile first,second;
        check(store.generate(project,QStringLiteral("中文用户定标一"),first,&error),qPrintable(error));
        check(store.copy(first.filename,QStringLiteral("中文用户定标二"),second,&error),qPrintable(error));
        auto policy=std::make_shared<CalibrationService>(root);policy->setDevice(true,project.deviceIdentity);policy->selectFile(first.filename);policy->synchronize();
        Ground::Session::GroundRecordingService recorder;recorder.setCalibrationPolicy(policy);Ground::Session::GroundRecordingOptions options;
        options.baseDirectory=QDir(root).filePath("data");check(recorder.start(options,nullptr,&error),qPrintable(error));
        check(recorder.recordFpgaHarmonics(1700000000000000ULL,cycle(816,2,987)),"installed fixture records an actual canonical Session feature");
        const auto session=recorder.status().sessionDirectory;check(!recorder.stop().writeFailed,"installed history is fully committed");
        auto unfinished=ConcentrationStage::create(300);unfinished.status="paused";unfinished.startedUtc=calibrationUtcNow();
        project.stages.append(unfinished);project.invalidateFits();check(store.saveDraft(project,&error),qPrintable(error));
        QJsonObject hashes;for(const auto& folder:{store.directory(),QDir(root).filePath("data")}) {
            QDirIterator iterator(folder,QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);
            while(iterator.hasNext()) {const auto path=iterator.next();hashes[QDir(root).relativeFilePath(path)]=QString::fromLatin1(QCryptographicHash::hash(read(path),QCryptographicHash::Sha256).toHex());}
        }
        write(manifestPath,QJsonDocument(QJsonObject{{"files",hashes},{"history",QDir(root).relativeFilePath(QDir(session).filePath("calibration_snapshot.json"))}}).toJson());
    }
    const auto manifest=QJsonDocument::fromJson(read(manifestPath)).object();const auto hashes=manifest["files"].toObject();
    check(!hashes.isEmpty(),"deployment fixture inventory is nonempty");
    for(auto it=hashes.begin();it!=hashes.end();++it)check(QString::fromLatin1(QCryptographicHash::hash(read(QDir(root).filePath(it.key())),QCryptographicHash::Sha256).toHex())==it.value().toString(),"install lifecycle preserves every user-data byte");
    check(store.files().size()==2&&store.drafts().size()==1,"both Chinese vcal files and unfinished task remain present");
    for(const auto& file:store.files())check(store.read(file,false,&error).valid,qPrintable(error));
    check(store.read(store.drafts().first(),true,&error).valid,qPrintable(error));
    check(Ground::Calibration::readRecordedConcentrations(QDir(root).filePath(manifest["history"].toString())).success,"installed Session history independently validates");
    report({{"phase","deployment_fixture"},{"verify",verify},{"user_files",hashes.size()}});
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.value(1) == "--fault-child") faultChild(args.value(2),args.value(3));
    else if (args.value(1) == "--faults") processFaults();
    else if (args.value(1) == "--journal") journal();
    else if (args.value(1) == "--filesystem-faults") filesystemFaults();
    else if (args.value(1) == "--deployment-fixture") deploymentFixture(args.value(2),false);
    else if (args.value(1) == "--verify-deployment-fixture") deploymentFixture(args.value(2),true);
    else if (args.value(1) == "--stress") {featureContract();rates();interruptions();failuresAndShutdown();throughput(100000);overloadedCheckpoint();}
    else if (args.value(1) == "--capacity") capacity(args.value(2).toInt(), args.value(3, "10").toInt());
    else throughput(args.value(2, "20000").toInt());
    return 0;
}
