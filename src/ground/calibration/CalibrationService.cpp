#include "CalibrationService.h"
#include "shared/session/CalibrationRuntime.h"
#include "shared/session/SessionManifest.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopeGuard>
#include <chrono>
#include <cmath>
#include <future>

namespace VaporView::Ground::Calibration
{
namespace
{
constexpr quint64 StreamGapMs=2000;
quint64 monotonicMs() { return quint64(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
bool atomicJson(const QString& filename,const QJsonObject& j,QString *error)
{
    QDir().mkpath(QFileInfo(filename).absolutePath());QSaveFile file(filename);file.setDirectWriteFallback(false);
    const auto b=QJsonDocument(j).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly) || file.write(b)!=b.size() || !file.commit()) { *error=QStringLiteral("无法保存定标锁/快照 %1：%2").arg(filename,file.errorString());return false; }return true;
}
}
CalibrationService::CalibrationService(QString root,Clock clock,QObject *parent)
    :QObject(parent),store_(root),clock_(clock?std::move(clock):Clock(monotonicMs))
{
    worker_=std::thread([this]{run();});
    submit([this]{restoreLock();listFiles();});
}
CalibrationService::~CalibrationService()
{
    {std::lock_guard lock(mutex_);stopping_=true;wake_.notify_all();}
    if(worker_.joinable())worker_.join();
}
CalibrationView CalibrationService::view() const { std::lock_guard lock(mutex_);auto v=state_;v.locked|=externalSessionOpen_;return v; }
void CalibrationService::submit(std::function<void()> job)
{
    std::lock_guard lock(mutex_);
    if(commands_.size()>=32) {message(QStringLiteral("定标操作队列繁忙，请稍后重试"),true);return;}
    commands_.push_back(std::move(job));wake_.notify_all();
}
void CalibrationService::synchronize()
{
    auto done=std::make_shared<std::promise<void>>();auto future=done->get_future();
    {
        std::unique_lock lock(mutex_);
        wake_.wait(lock,[this]{return stopping_ || commands_.size()<32;});
        if(stopping_)return;
        commands_.push_back([this,done]{tick();if(dirty_)persist();done->set_value();});wake_.notify_all();
    }
    future.wait();
}
void CalibrationService::run()
{
    std::unique_lock lock(mutex_);
    while(true) {
        wake_.wait_for(lock,std::chrono::milliseconds(100));
        if(overflowed_){overflowed_=false;pause(QStringLiteral("峰值队列达到上限，已暂停采集，请检查磁盘或数据速率"));}
        while(!peaks_.empty()) {auto p=std::move(peaks_.front());peaks_.pop_front();process(p);}
        while(!commands_.empty()) {auto job=std::move(commands_.front());commands_.pop_front();wake_.notify_all();job();emit changed();}
        tick();
        if(stopping_) {if(state_.acquiring)pause(QStringLiteral("应用退出，阶段已暂停"));if(dirty_)persist();break;}
    }
}
void CalibrationService::message(const QString& s,bool error) {state_.message=s;state_.error=error;emit changed();}
void CalibrationService::offer(const PeakRecord& r)
{
    std::lock_guard lock(mutex_);
    if(peaks_.size()>=4096) {overflowed_=true;wake_.notify_all();return;}
    peaks_.push_back({r,clock_()});wake_.notify_all();
}
void CalibrationService::setDevice(bool connected,const QJsonObject& identity)
{
    submit([this,connected,identity]{
        if(state_.acquiring && (!connected || device_!=identity))pause(QStringLiteral("设备断开或身份变化，请核对后重新确认稳定并继续"));
        if(state_.connected!=connected || device_!=identity) {
            latestValid_.clear();sensorReadings_={};state_.live.clear();state_.recentPeaks.clear();environment_={};alignment_="unavailable";
        }
        state_.connected=connected;device_=identity;
        state_.identityWarning=state_.selected.valid && !state_.selected.project.deviceIdentity.isEmpty() && state_.selected.project.deviceIdentity!=device_
            ?QStringLiteral("当前设备与所选定标文件身份不同，开始录制前必须核对并确认"):QString();
    });
}
void CalibrationService::setEnvironment(const Environment& e,const QString& a)
{ submit([this,e,a]{environment_=e;alignment_=a;}); }
void CalibrationService::setExternalSessionOpen(bool open)
{
    submit([this,open]{externalSessionOpen_=open;if(open && state_.acquiring)pause(QStringLiteral("远程 Session 已开始或暂停，定标采集已停止计时"));});
}
void CalibrationService::noteSensor(const QString& source,const QJsonObject& reading)
{std::lock_guard lock(mutex_);sensorReadings_[source]=reading;}
bool CalibrationService::editable()
{
    if(state_.acquiring || state_.locked || externalSessionOpen_) {message(QStringLiteral("采集或 Session 锁定期间不能修改任务"),true);return false;}return true;
}
void CalibrationService::createProject(const QString& name,const QString& unit)
{
    submit([this,name,unit]{if(!editable())return;if(dirty_ && !persist())return;
        auto p=CalibrationProject::create(name,unit);const auto error=p.validate();if(!error.isEmpty()){message(error,true);return;}
        state_.project=p;state_.activeStage=-1;recordedIds_.clear();dirty_=true;persist();message(QStringLiteral("新任务已创建并保存草稿"));listFiles();});
}
void CalibrationService::addStage(double c,quint64 ms,const QString& note)
{
    submit([this,c,ms,note]{if(!editable())return;
        if(state_.project.id.isEmpty() || !std::isfinite(c) || c<0 || !ms){message(QStringLiteral("请先创建任务并填写非负浓度及采集时长"),true);return;}
        auto s=ConcentrationStage::create(c,ms);s.note=note;state_.project.stages.append(s);state_.project.invalidateFits();dirty_=true;persist();});
}
void CalibrationService::editStage(int i,double c,quint64 ms,const QString& note)
{
    submit([this,i,c,ms,note]{if(!editable() || i<0 || i>=state_.project.stages.size())return;
        auto& s=state_.project.stages[i];if(s.status!="pending"){message(QStringLiteral("已开始的阶段不能修改浓度/时长，请新建阶段或废弃后重采"),true);return;}
        if(!std::isfinite(c) || c<0 || !ms){message(QStringLiteral("浓度或时长无效"),true);return;}
        s.concentration=c;s.configuredMs=ms;s.note=note;state_.project.invalidateFits();dirty_=true;persist();});
}
void CalibrationService::moveStage(int i,int delta)
{
    submit([this,i,delta]{if(!editable())return;int j=i+delta;
        if(i<0 || j<0 || i>=state_.project.stages.size() || j>=state_.project.stages.size())return;
        state_.project.stages.swapItemsAt(i,j);state_.activeStage=-1;state_.project.invalidateFits();dirty_=true;persist();});
}
void CalibrationService::removeStage(int i)
{
    submit([this,i]{if(!editable() || i<0 || i>=state_.project.stages.size())return;
        if(state_.project.stages[i].status=="pending")state_.project.stages.removeAt(i);
        else {state_.project.stages[i].status="abandoned";message(QStringLiteral("阶段已废弃，原始记录保留；请添加新阶段重采"));}
        state_.activeStage=-1;state_.project.invalidateFits();dirty_=true;persist();});
}
QString CalibrationService::preflight(bool confirmed) const
{
    if(state_.locked || externalSessionOpen_)return QStringLiteral("普通 Session 正在录制、暂停或等待异常恢复处理");
    if(state_.acquiring)return QStringLiteral("已有定标采集运行");
    if(!state_.connected)return QStringLiteral("设备未连接");
    if(device_.isEmpty())return QStringLiteral("设备身份未知，请重新连接");
    for(int w:{816,1368}) if(!latestValid_.contains(w) || clock_()-latestValid_[w]>StreamGapMs)
        return QStringLiteral("%1 nm 未产生近期完整有效周期，请启动两个 DLIA 通道并检查 FPGA 数据链路").arg(w);
    if(!state_.project.deviceIdentity.isEmpty() && state_.project.deviceIdentity!=device_ && !confirmed)
        return QStringLiteral("设备身份与任务不一致；禁止混入不同设备数据，请恢复原设备或新建任务");
    return state_.project.validate();
}
void CalibrationService::startStage(int i,bool confirmed)
{
    submit([this,i,confirmed]{
        auto error=preflight(confirmed);if(!error.isEmpty()){message(error,true);return;}
        if(i<0 || i>=state_.project.stages.size())return;
        auto& s=state_.project.stages[i];if(s.status=="complete" || s.status=="abandoned"){message(QStringLiteral("阶段已完成/废弃；如需重采请新建阶段"),true);return;}
        // A confirmation cannot merge data from a different device into one task.
        if(!state_.project.deviceIdentity.isEmpty() && state_.project.deviceIdentity!=device_){message(QStringLiteral("请使用原设备恢复此任务，或创建新任务"),true);return;}
        if(state_.activeStage==i && s.status=="paused")s.wallMs+=clock_()-wallCursor_;
        else if(!s.startedUtc.isEmpty())s.wallMs=std::max(s.wallMs,quint64(std::max<qint64>(0,QDateTime::fromString(s.startedUtc,Qt::ISODateWithMs).msecsTo(QDateTime::currentDateTimeUtc()))));
        state_.project.deviceIdentity=device_;state_.activeStage=i;
        if(QString::fromUtf8(QJsonDocument(device_).toJson(QJsonDocument::Compact)).contains("simulator",Qt::CaseInsensitive))state_.project.origin="software_test";
        if(s.startedUtc.isEmpty())s.startedUtc=calibrationUtcNow();
        s.status="acquiring";state_.acquiring=true;state_.project.invalidateFits();
        cursor_=wallCursor_=clock_();latestValid_[816]=latestValid_[1368]=cursor_;dirty_=true;
        if(!persist()){pause(QStringLiteral("草稿无法保存，未开始采集"));return;}
        message(QStringLiteral("正在同步采集 816 nm / 1368 nm；仅累计两路正常数据覆盖的有效时间"));
    });
}
void CalibrationService::pause(const QString& why,bool stopped)
{
    if(state_.activeStage>=0 && state_.activeStage<state_.project.stages.size() &&
       (state_.acquiring || state_.project.stages[state_.activeStage].status=="paused")) {
        auto& s=state_.project.stages[state_.activeStage];const auto now=clock_();s.wallMs+=now-wallCursor_;wallCursor_=now;
        s.status=stopped?"incomplete":"paused";dirty_=true;
    }
    state_.acquiring=false;cursor_=0;message(why);
}
void CalibrationService::pauseStage(bool stopped)
{submit([this,stopped]{pause(stopped?QStringLiteral("已停止，未完成阶段和原始数据已保留"):QStringLiteral("阶段已暂停"),stopped);persist();});}
void CalibrationService::process(const PendingPeak& p)
{
    const auto& r=p.record;const int w=r.feature.channel==0x31?816:1368;
    if(!r.feature.sourceError().isEmpty() || !state_.connected || r.deviceIdentity!=device_)return;
    const bool valid=r.feature.invalidReason().isEmpty();
    state_.recentPeaks[w]=r.feature.peak;
    const auto previous=latestValid_.value(w);
    if(valid)latestValid_[w]=p.time;else latestValid_.remove(w);
    if(state_.selected.valid) {
        Sample sample;sample.feature=r.feature;sample.environment=r.environment;
        sample.origin=state_.selected.project.origin=="software_test"?"software_test":"unverified";
        auto f=state_.selected.project.calculationFit(w);state_.live[w]=calculate(sample,w,&f);
    }
    if(!state_.acquiring)return;
    if(previous && p.time-previous>StreamGapMs){pause(QStringLiteral("实时数据流中断，有效计时已暂停，请重新确认稳定"));return;}
    if(recordedIds_.contains(r.feature.id))return;
    auto& s=state_.project.stages[state_.activeStage];auto stored=r;stored.taskId=state_.project.id;stored.stageId=s.id;
    for(auto i=sensorReadings_.begin();i!=sensorReadings_.end();++i) {
        const bool remote=r.deviceIdentity["transport"]=="remote_fpga";
        if(i.key().startsWith("remote_")!=remote)continue;
        auto reading=i.value().toObject();const auto receipt=reading["host_received_utc_us"].toString().toULongLong();
        const auto delta=receipt>r.feature.hostTimeUs?receipt-r.feature.hostTimeUs:r.feature.hostTimeUs-receipt;
        if(receipt && delta<=EnvironmentWindowUs) {reading["peak_delta_us"]=QString::number(delta);stored.sensorReadings[i.key()]=reading;stored.alignment="host_receipt_uncertain";}
    }
    // Host reception summaries do not assert sensor sample-time alignment.
    if(stored.alignment=="unavailable" && environment_.snapshotTimeUs) {
        const auto delta=r.feature.hostTimeUs>environment_.snapshotTimeUs?r.feature.hostTimeUs-environment_.snapshotTimeUs:environment_.snapshotTimeUs-r.feature.hostTimeUs;
        if(delta<=EnvironmentWindowUs){stored.environment=environment_;stored.environment.deltaUs=delta;stored.alignment=alignment_;}
    }
    s.samples[w].append(stored);recordedIds_.insert(r.feature.id);dirty_=true;
    if(!valid){pause(QStringLiteral("收到质量无效周期，原始记录已保留；请检查链路并重新确认稳定"));return;}
    const auto end=std::min(latestValid_.value(816),latestValid_.value(1368));
    if(valid && end>cursor_ && p.time-latestValid_.value(816)<=StreamGapMs && p.time-latestValid_.value(1368)<=StreamGapMs) {
        s.effectiveMs+=std::min(end-cursor_,s.configuredMs-s.effectiveMs);cursor_=end;
    }
    if(s.effectiveMs==s.configuredMs) {
        s.wallMs+=p.time-wallCursor_;s.status="complete";s.completedUtc=calibrationUtcNow();state_.acquiring=false;wallCursor_=p.time;
        persist();message(QStringLiteral("当前浓度阶段已完成；更换标气并确认稳定后再开始下一阶段"));
    }
}
void CalibrationService::tick()
{
    const auto now=clock_();
    for(int w:{816,1368})if(!latestValid_.contains(w) || now-latestValid_.value(w)>StreamGapMs)state_.live.remove(w);
    if(!state_.acquiring && state_.activeStage>=0 && state_.project.stages[state_.activeStage].status=="paused") {
        state_.project.stages[state_.activeStage].wallMs+=now-wallCursor_;wallCursor_=now;
    }
    if(state_.acquiring) {
        auto& s=state_.project.stages[state_.activeStage];s.wallMs+=now-wallCursor_;wallCursor_=now;dirty_=true;
        if(!state_.connected || now-latestValid_.value(816)>StreamGapMs || now-latestValid_.value(1368)>StreamGapMs)
            pause(QStringLiteral("设备或双路数据流异常，采集计时已暂停"));
    }
    if(dirty_ && now-lastSave_>=1000) {persist();emit changed();}
    if(now-lastNotify_>=500){lastNotify_=now;emit changed();}
}
bool CalibrationService::persist()
{
    if(state_.project.id.isEmpty())return true;
    const auto snapshot=state_.project;QString error;bool saved=false;
    {mutex_.unlock();const auto restore=qScopeGuard([this]{mutex_.lock();});saved=store_.saveDraft(snapshot,&error);}
    if(!saved) {state_.acquiring=false;
        if(state_.activeStage>=0 && state_.project.stages[state_.activeStage].status=="acquiring")state_.project.stages[state_.activeStage].status="paused";
        message(error,true);return false;}
    dirty_=false;lastSave_=clock_();return true;
}
void CalibrationService::exclude(const QString& stage,int w,const QSet<QString>& ids,bool excluded,const QString& reason)
{submit([this,stage,w,ids,excluded,reason]{if(!editable())return;QString e;if(!state_.project.exclude(stage,w,ids,excluded,reason,&e)){message(e,true);return;}dirty_=true;persist();message(QStringLiteral("审核记录已保存，统计已更新；请重新拟合"));});}
void CalibrationService::fitProject()
{submit([this]{if(!editable())return;auto project=state_.project;const auto errors=outsideStateLock([&]{return project.fit();});state_.project=project;QStringList result;
    for(int w:{816,1368})result.append(QString::number(w)+" nm: "+(errors.contains(w)?errors[w]:QStringLiteral("拟合成功 ")+state_.project.fits[w].warning));
    dirty_=true;persist();message(result.join('\n'),!errors.isEmpty());});}
void CalibrationService::listFiles() {const auto paths=outsideStateLock([this]{return qMakePair(store_.files(),store_.drafts());});state_.files=paths.first;state_.drafts=paths.second;}
void CalibrationService::refreshFiles() {submit([this]{listFiles();});}
void CalibrationService::inspectFile(const QString& filename)
{submit([this,filename]{QString error;const auto f=outsideStateLock([&]{return store_.read(filename,false,&error);});
    if(!f.valid){message(error,true);return;}state_.inspectedDocument=f.document;++state_.inspection;message(QStringLiteral("文件完整性及双波长参数已校验：%1").arg(filename));});}
void CalibrationService::generateFile(const QString& name)
{submit([this,name]{if(!editable())return;QString e;CalibrationFile f;const auto project=state_.project;if(!persist() || !outsideStateLock([&]{return store_.generate(project,name,f,&e);})){if(!e.isEmpty())message(e,true);return;}listFiles();message(QStringLiteral("定标文件已生成：%1").arg(f.filename));});}
void CalibrationService::openProject(const QString& filename,bool draft)
{
    submit([this,filename,draft]{if(!editable())return;if(dirty_ && !persist())return;QString e;auto f=outsideStateLock([&]{return store_.read(filename,draft,&e);});
        if(!f.valid){message(e,true);return;}auto p=f.project;
        if(!draft){p.parentFileId=f.id;p.id=calibrationUuid();p.createdUtc=calibrationUtcNow();
            if(p.origin!="legacy_import")p.invalidateFits();}
        state_.project=p;state_.activeStage=-1;recordedIds_.clear();
        for(int i=0;i<p.stages.size();++i){auto& s=state_.project.stages[i];
            for(const auto& rows:s.samples)for(const auto& r:rows)recordedIds_.insert(r.feature.id);
            if(s.status=="acquiring")s.status="paused";
            if(s.status=="paused" || s.status=="incomplete")s.wallMs=std::max(s.wallMs,quint64(std::max<qint64>(0,QDateTime::fromString(s.startedUtc,Qt::ISODateWithMs).msecsTo(QDateTime::currentDateTimeUtc()))));
        }
        dirty_=true;persist();message(QStringLiteral("草稿/修订任务已加载；恢复前请核对设备及标气稳定状态"));
    });
}
void CalibrationService::selectFile(const QString& filename,bool confirmed)
{
    submit([this,filename,confirmed]{if(state_.acquiring || state_.locked || externalSessionOpen_){message(QStringLiteral("定标采集或活动 Session 期间禁止切换定标文件"),true);return;}
        if(QFileInfo(filename).absolutePath()!=QFileInfo(store_.directory()).absoluteFilePath()){message(QStringLiteral("请先将外部 .vcal 导入安装目录 calibrations，再选择用于测量"),true);return;}
        QString e;auto f=outsideStateLock([&]{return store_.read(filename,false,&e);});if(!f.valid){message(e,true);return;}
        const bool mismatch=state_.connected && !f.project.deviceIdentity.isEmpty() && f.project.deviceIdentity!=device_;
        if(mismatch && !confirmed){message(QStringLiteral("定标文件设备身份不一致，请核对详情后明确确认使用"),true);return;}
        if(!atomicJson(QDir(store_.directory()).filePath("selection.json"),{{"filename",f.filename},{"sha256",f.sha256}},&e)){message(e,true);return;}
        state_.selected=f;state_.identityWarning=mismatch?QStringLiteral("设备身份不一致，用户已核对并确认使用"):QString();state_.live.clear();
        message(QStringLiteral("已选择 %1；验证用线性模型，初始浓度未进行温压补偿").arg(QFileInfo(filename).fileName())+(mismatch?"\n"+state_.identityWarning:QString()));
    });
}
void CalibrationService::copyFile(const QString& filename,const QString& name)
{submit([this,filename,name]{QString e;CalibrationFile f;if(!outsideStateLock([&]{return store_.copy(filename,name,f,&e);}))message(e,true);else{listFiles();message(QStringLiteral("已另存为：%1").arg(f.filename));}});}
void CalibrationService::removeFile(const QString& filename,bool archive)
{submit([this,filename,archive]{QString e;const auto locked=(state_.locked || externalSessionOpen_)?state_.selected.sha256:QString();
    if(!(archive?store_.archive(filename,locked,&e):store_.remove(filename,locked,&e)))message(e,true);
    else{if(state_.selected.filename==filename){state_.selected={};state_.live.clear();}listFiles();message(archive?QStringLiteral("文件已归档"):QStringLiteral("文件已删除"));}});}
void CalibrationService::importLegacy(const QString& filename,const QString& a,const QString& b)
{submit([this,filename,a,b]{if(!editable())return;Library library;QString e;if(!Store(filename).load(library,&e)){message(e,true);return;}
    const Fit *fa=nullptr,*fb=nullptr;for(const auto& f:library.fits){if(f.id==a)fa=&f;if(f.id==b)fb=&f;}
    if(!fa || !fb){message(QStringLiteral("请选择两路原始 Fit ID，不能用一路补齐另一路"),true);return;}
    auto p=CalibrationProject::importLegacy(*fa,*fb,&e);if(!e.isEmpty()){message(e,true);return;}
    state_.project=p;state_.activeStage=-1;recordedIds_.clear();dirty_=true;persist();message(QStringLiteral("旧版参数已导入；缺失的采集阶段未伪造，原始 library.json 保留"));});}
bool CalibrationService::saveLock(QString *error)
{
    return atomicJson(QDir(store_.draftsDirectory()).filePath("session_lock.json"),
        {{"kind","vaporview_session_calibration_lock"},{"snapshot",lockSnapshot_},{"session_directory",sessionDirectory_}},error);
}
void CalibrationService::restoreLock()
{
    QFile file(QDir(store_.draftsDirectory()).filePath("session_lock.json"));
    if(file.exists()) {
        state_.locked=true;state_.recoveredSession=QStringLiteral("未正常结束的 Session；请检查后结束异常会话");
        if(file.open(QIODevice::ReadOnly)) {
            const auto j=QJsonDocument::fromJson(file.readAll()).object();lockSnapshot_=j["snapshot"].toObject();sessionDirectory_=j["session_directory"].toString();
            QString e;auto f=CalibrationFileStore::decode(QJsonDocument(lockSnapshot_["vcal_document"].toObject()).toJson(QJsonDocument::Compact),false,&e);
            if(f.valid){f.filename=lockSnapshot_["filename"].toString();f.sha256=lockSnapshot_["file_sha256"].toString();state_.selected=f;}
        }
        message(QStringLiteral("已恢复异常 Session 的定标锁，暂停/重启不会自动解除"),true);return;
    }
    QFile selection(QDir(store_.directory()).filePath("selection.json"));
    if(selection.open(QIODevice::ReadOnly)) {
        const auto j=QJsonDocument::fromJson(selection.readAll()).object();QString e;auto f=store_.read(j["filename"].toString(),false,&e);
        if(f.valid && f.sha256==j["sha256"].toString())state_.selected=f;
    }
}
bool CalibrationService::prepare(QJsonObject& snapshot,QString *error)
{
    auto done=std::make_shared<std::promise<bool>>();auto future=done->get_future();QString localError;auto *message=error?error:&localError;
    synchronize();
    {std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_ || commands_.size()<32;});
        if(stopping_){*message=QStringLiteral("定标服务正在退出");return false;}
        commands_.push_back([this,&snapshot,message,done]{done->set_value(prepareImpl(snapshot,message));});wake_.notify_all();}
    return future.get();
}
bool CalibrationService::prepareImpl(QJsonObject& snapshot,QString *error)
{
    if(state_.locked || externalSessionOpen_ || state_.acquiring || !state_.connected || !state_.selected.valid) {
        *error=QStringLiteral("正式录制需要已连接设备、有效双波长 .vcal，且没有定标采集或未结束 Session");return false;}
    const auto selectedFilename=state_.selected.filename;auto f=outsideStateLock([&]{return store_.read(selectedFilename,false,error);});if(!f.valid)return false;
    if(f.sha256!=state_.selected.sha256){*error=QStringLiteral("所选定标文件已变化，请重新选择并核对");return false;}
    if(!state_.identityWarning.isEmpty() && !state_.identityWarning.contains(QStringLiteral("用户已核对"))){*error=state_.identityWarning;return false;}
    lockSnapshot_={{"kind","vaporview_session_calibration"},{"schema_version",1},{"file_id",f.id},{"file_sha256",f.sha256},
        {"filename",QFileInfo(f.filename).fileName()},{"task_name",f.project.name},{"vcal_document",f.document},
        {"identity_warning",state_.identityWarning}};
    QJsonObject channels;
    for(int w:{816,1368}) {
        const auto& p=f.project.fits[w].parameters;
        channels[QString::number(w)]=QJsonObject{{"wavelength",w},{"channel",int(channelForWavelength(w))},{"unit",f.project.unit},
            {"model",p.model},{"model_version",p.modelVersion},{"parameter_id",p.id},{"a",p.a},{"b",p.b},
            {"r_min",p.rMin},{"r_max",p.rMax},{"c_min",p.cMin},{"c_max",p.cMax},
            {"sample_count",p.sampleCount},{"concentration_count",p.concentrationCount},{"r_squared",p.rSquared},{"rmse",p.rmse}};
    }
    bool softwareTest=f.project.origin=="software_test";
    for(const auto& oldFit:f.project.legacyFits)for(const auto& sample:oldFit.dataset.samples)softwareTest|=sample.origin=="software_test";
    QJsonObject runtime{{"schema_version",1},{"kind","water_runtime_parameters"},{"file_id",f.id},{"file_sha256",f.sha256},
        {"task_id",f.project.id},{"task_name",f.project.name},{"filename",QFileInfo(f.filename).fileName()},
        {"created_utc",f.createdUtc},{"device_identity",f.project.deviceIdentity},{"hardware_mapping",f.project.hardwareMapping},
        {"software_version",f.project.softwareVersion},{"origin",softwareTest?QStringLiteral("software_test"):f.project.origin},{"source_origin",f.project.origin},{"channels",channels},
        {"peak_algorithm","pointwise_h2_over_h1_v1"},{"model","validation_linear_v1"},{"compensation","unconfigured"}};
    runtime["sha256"]=QString::fromLatin1(VaporView::Session::runtimeCalibrationDigest(runtime));
    *error=VaporView::Session::validateRuntimeCalibration(runtime);if(!error->isEmpty())return false;
    lockSnapshot_["runtime"]=runtime;
    sessionDirectory_.clear();if(!saveLock(error))return false;state_.locked=true;snapshot=lockSnapshot_;emit changed();return true;
}
bool CalibrationService::bindSession(const QString& directory,QString *error)
{std::lock_guard lock(mutex_);sessionDirectory_=directory;return saveLock(error);}
void CalibrationService::finish()
{
    std::lock_guard lock(mutex_);const auto file=QDir(store_.draftsDirectory()).filePath("session_lock.json");
    if(QFileInfo::exists(file) && !QFile::remove(file)){message(QStringLiteral("无法删除持久化定标锁，请检查目录权限"),true);return;}
    state_.locked=false;state_.recoveredSession.clear();sessionDirectory_.clear();lockSnapshot_={};emit changed();
}
void CalibrationService::finishRecoveredSession()
{submit([this]{
    if(state_.recoveredSession.isEmpty()){message(QStringLiteral("只有异常恢复的会话可在此结束"),true);return;}
    if(!sessionDirectory_.isEmpty()) {
        const auto filename=QDir(sessionDirectory_).filePath(VaporView::Session::standardSessionPackageLayout().manifestPath);QFile file(filename);
        if(!file.open(QIODevice::ReadOnly)){message(QStringLiteral("无法读取异常会话清单，定标锁保留：%1").arg(file.errorString()),true);return;}
        auto j=QJsonDocument::fromJson(file.readAll()).object();file.close();
        if(j["session_format"]!="vaporview.session"){message(QStringLiteral("异常会话清单格式无效，定标锁保留"),true);return;}
        if(j["state"]!="complete") {
            j["state"]="incomplete";j["end_time_utc"]=calibrationUtcNow();j["end_time_us"]=QString::number(quint64(QDateTime::currentMSecsSinceEpoch())*1000);
            QString error;if(!atomicJson(filename,j,&error)){message(error,true);return;}
        }
    }
    finish();if(!state_.locked)message(QStringLiteral("异常会话已明确结束并标记未完成，历史快照保留"));
});}
QJsonObject CalibrationService::concentration(quint64 host,quint64 sequence,const VaporView::Session::HarmonicLayout& l,const FpgaWave::NormalizedFeature& n)
{
    std::lock_guard lock(mutex_);if(!state_.locked || !state_.selected.valid)return {};
    return VaporView::Session::calculateRuntimeConcentration(lockSnapshot_["runtime"].toObject(),sessionDirectory_,host,sequence,l,n);
}
}
