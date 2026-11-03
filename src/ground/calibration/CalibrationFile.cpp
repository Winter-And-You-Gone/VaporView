#include "CalibrationFile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace VaporView::Ground::Calibration
{
namespace
{
QJsonObject document(const CalibrationProject& p,const QString& kind)
{
    QJsonObject j{{"kind",kind},{"schema_version",1},{"file_id",calibrationUuid()},
        {"created_utc",calibrationUtcNow()},{"project",p.toJson()}};
    j["sha256"]=QString::fromLatin1(calibrationDigest(j));return j;
}
QString ioError(const QString& name,const QString& detail)
{
    return QStringLiteral("无法写入安装目录定标文件 %1：%2。请检查磁盘空间及该安装目录的写权限，或运行安装程序修复权限；未改存到其他目录。").arg(name,detail);
}
QString fileStamp(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() ? QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch()) : QString();
}
QMap<QString, QMap<int, int>> recordCounts(const CalibrationProject& project)
{
    QMap<QString, QMap<int, int>> counts;
    for (const auto& stage : project.stages)
        for (int wave : {816, 1368}) counts[stage.id][wave] = stage.samples.value(wave).size();
    return counts;
}
QJsonObject documentSummary(QJsonObject document)
{
    auto project=document["project"].toObject();auto stages=project["stages"].toArray();
    for(int index=0;index<stages.size();++index) {
        auto stage=stages[index].toObject();auto channels=stage["channels"].toObject();
        for(const auto& wave:channels.keys())channels[wave]=QJsonObject{{"raw_sample_count",channels[wave].toArray().size()}};
        stage["channels"]=channels;stage["audit_operation_count"]=stage.take("exclusions").toArray().size();stages[index]=stage;
    }
    project["stages"]=stages;document["project"]=project;return document;
}
}
QJsonObject CalibrationFile::document() const
{
    return QJsonDocument::fromJson(qUncompress(compressedDocument_)).object();
}
CalibrationFileStore::CalibrationFileStore(QString root)
    :root_(root.isEmpty()?QCoreApplication::applicationDirPath():QFileInfo(root).absoluteFilePath()) {}
QString CalibrationFileStore::directory() const {return QDir(root_).filePath("calibrations");}
QString CalibrationFileStore::draftsDirectory() const {return QDir(directory()).filePath("drafts");}
QStringList CalibrationFileStore::files() const
{
    QStringList out;for(const auto& f:QDir(directory()).entryList({"*.vcal"},QDir::Files,QDir::Name))out.append(QDir(directory()).filePath(f));return out;
}
QStringList CalibrationFileStore::drafts() const
{
    QStringList out;for(const auto& f:QDir(draftsDirectory()).entryList({"*.vcaldraft"},QDir::Files,QDir::Time))out.append(QDir(draftsDirectory()).filePath(f));return out;
}
bool CalibrationFileStore::write(const QString& path,const QJsonObject& j,bool replace,QString *error,bool acquisition) const
{
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) { *error=ioError(path,QStringLiteral("目录创建失败"));return false; }
    QLockFile lock(QDir(directory()).filePath(".write.lock"));
    if(!lock.tryLock(0)) { *error=QStringLiteral("另一个进程正在写定标文件，请稍后重试");return false; }
    if(acquisition && fileStamp(progress_.filename)!=progress_.stamp) {
        *error=QStringLiteral("草稿检查点被外部修改，已暂停；请保留草稿及 .parts 目录并核对后恢复");return false;
    }
    if(!replace && QFileInfo::exists(path)) { *error=QStringLiteral("定标文件已存在，不能覆盖：%1").arg(path);return false; }
    QSaveFile file(path);file.setDirectWriteFallback(false);const auto bytes=QJsonDocument(j).toJson(QJsonDocument::Compact);
    if(bytes.size()>256*1024*1024){*error=ioError(path,QStringLiteral("任务超过当前 schema 的 256 MiB 单文件上限，请保留草稿并缩短新阶段时长"));return false;}
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        *error=ioError(path,file.errorString());return false;
    }
    return true;
}
bool CalibrationFileStore::saveDraft(const CalibrationProject& p,QString *error) const
{
    return checkpoint(p,error,false);
}
quint64 CalibrationFileStore::draftStorageBytes() const
{
    return quint64(QFileInfo(progress_.filename).size())+progress_.bytes;
}
bool CalibrationFileStore::checkpoint(const CalibrationProject& p,QString *error,bool acquisition) const
{
    const auto e=p.validate();if(!e.isEmpty()) { *error=e;return false; }
    const auto path=QDir(draftsDirectory()).filePath("task_"+p.id+".vcaldraft");
    const auto partsPath=path+QStringLiteral(".parts");
    const auto oldParts=QDir(partsPath).entryList({"*.vcalpart"},QDir::Files);
    const auto checkpoint=document(p,"vaporview_calibration_draft");
    if(!write(path,checkpoint,true,error,acquisition))return false;
    progress_={p.id,path,checkpoint["file_id"].toString(),fileStamp(path),{},0,0,recordCounts(p)};
    // The new file ID makes old parts obsolete atomically, before cleanup.
    for(const auto& part:oldParts)QFile::remove(QDir(partsPath).filePath(part));
    QDir().rmdir(partsPath);
    return true;
}
bool CalibrationFileStore::saveAcquisitionProgress(const CalibrationProject& p,QString *error) const
{
    // Keep append-only parts throughout acquisition. Compact on explicit full
    // saves (pause, edit, next stage or shutdown), never periodically rewrite
    // a growing task on the acquisition worker.
    if(progress_.taskId!=p.id)
        return checkpoint(p,error,progress_.taskId==p.id);
    const auto counts=recordCounts(p);
    if(counts.keys()!=progress_.counts.keys())return checkpoint(p,error,true);
    CalibrationProject metadata=p;
    for(auto& stage:metadata.stages)stage.samples.clear();
    auto project=metadata.toJson();auto stages=project["stages"].toArray();QJsonObject baseCounts;
    for(int index=0;index<p.stages.size();++index) {
        const auto& source=p.stages[index];auto stage=stages[index].toObject();QJsonObject channels,statistics,base;
        for(int wave:{816,1368}) {
            const auto rows=source.samples.value(wave);const int before=progress_.counts.value(source.id).value(wave);
            if(rows.size()<before)return checkpoint(p,error,true);
            QJsonArray appended;
            for(int row=before;row<rows.size();++row) {
                const auto& record=rows[row];
                const auto invalid=record.feature.sourceError();
                if(!invalid.isEmpty() || record.stageId!=source.id || record.taskId!=p.id ||
                    record.deviceIdentity!=p.deviceIdentity || record.feature.channel!=channelForWavelength(wave)) {
                    *error=QStringLiteral("增量草稿含无效来源记录：%1").arg(invalid);return false;
                }
                appended.append(record.toJson());
            }
            channels[QString::number(wave)]=appended;statistics[QString::number(wave)]=source.statistics(wave).toJson();
            base[QString::number(wave)]=before;
        }
        stage["channels"]=channels;stage["statistics"]=statistics;stages[index]=stage;baseCounts[source.id]=base;
    }
    project["stages"]=stages;
    QJsonObject part{{"kind","vaporview_calibration_progress"},{"schema_version",1},
        {"base_file_id",progress_.fileId},{"sequence",QString::number(progress_.sequence+1)},
        {"previous_sha256",progress_.head},{"base_counts",baseCounts},{"project",project}};
    const auto head=QString::fromLatin1(calibrationDigest(part));part["sha256"]=head;
    if(QJsonDocument(part).toJson(QJsonDocument::Compact).size()>16*1024*1024)
        return checkpoint(p,error,true);
    const auto filename=QDir(progress_.filename+".parts").filePath(progress_.fileId+'_'+
        QString::number(progress_.sequence+1).rightJustified(8,'0')+".vcalpart");
    if(!write(filename,part,false,error,true))return false;
    ++progress_.sequence;progress_.bytes+=QFileInfo(filename).size();progress_.head=head;progress_.counts=counts;
    return true;
}
QString CalibrationFileStore::uniqueFilename(const QString& requested) const
{
    auto name=requested.trimmed();if(name.endsWith(".vcal",Qt::CaseInsensitive))name.chop(5);
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")),"_");
    while(name.endsWith('.') || name.endsWith(' '))name.chop(1);
    name=name.left(150);if(name.isEmpty())name=QStringLiteral("定标文件");
    if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)",QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())name.prepend('_');
    auto path=QDir(directory()).filePath(name+".vcal");
    for(int n=2;QFileInfo::exists(path);++n)path=QDir(directory()).filePath(name+QStringLiteral(" (%1).vcal").arg(n));
    return path;
}
bool CalibrationFileStore::generate(const CalibrationProject& p,const QString& name,CalibrationFile& out,QString *error) const
{
    *error=p.validate(true);if(!error->isEmpty())return false;
    const auto j=document(p,"vaporview_calibration");
    if(!decode(QJsonDocument(j).toJson(QJsonDocument::Compact),false,error).valid)return false;
    const auto path=uniqueFilename(name);if(!write(path,j,false,error))return false;
    out=read(path,false,error);return out.valid;
}
CalibrationFile CalibrationFileStore::decode(QByteArray bytes,bool draft,QString *error,const CalibrationReadObserver& observer)
{
    error->clear();CalibrationFile f;QJsonParseError parse;
    if(bytes.size()>256*1024*1024) { *error=QStringLiteral("定标文件超过 256 MiB 限制");return f; }
    f.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    if(observer)observer("file_sha");
    f.compressedDocument_=qCompress(bytes,1);f.compressedDocument_.squeeze();
    if(observer)observer("source_cache");
    auto doc=QJsonDocument::fromJson(bytes,&parse);auto j=doc.object();
    bytes.clear();bytes.squeeze();
    if(observer)observer("json_parse");
    const auto hash=j.take("sha256").toString();
    if(parse.error!=QJsonParseError::NoError || !doc.isObject() || j["schema_version"]!=1 ||
       j["kind"]!=(draft?"vaporview_calibration_draft":"vaporview_calibration") || hash.toLatin1()!=calibrationDigest(j) ||
       QUuid(j["file_id"].toString()).isNull() || !QDateTime::fromString(j["created_utc"].toString(),Qt::ISODateWithMs).isValid()) { *error=QStringLiteral("定标文件损坏、SHA-256 不匹配或 schema 不兼容");return f; }
    if(observer)observer("canonical_sha");
    f.id=j["file_id"].toString();f.createdUtc=j["created_utc"].toString();
    f.summary=documentSummary(doc.object());
    auto project=j["project"].toObject();doc={};j={};
    f.project=CalibrationProject::fromJson(std::move(project),error,observer);if(!error->isEmpty())return f;
    *error=f.project.validate(!draft);if(!error->isEmpty())return f;
    f.valid=true;
    if(observer)observer("validated");
    return f;
}
CalibrationFile CalibrationFileStore::read(const QString& path,bool draft,QString *error,const CalibrationReadObserver& observer) const
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly)) { *error=QStringLiteral("无法读取定标文件 %1：%2").arg(path,file.errorString());return {}; }
    if(file.size()>256*1024*1024) { *error=QStringLiteral("定标文件过大");return {}; }
    auto bytes=file.readAll();
    if(observer)observer("file_read");
    auto f=decode(std::move(bytes),draft,error,observer);f.filename=QFileInfo(path).absoluteFilePath();
    if(!draft || !f.valid)return f;
    const auto partsDirectory=path+QStringLiteral(".parts");
    const auto parts=QDir(partsDirectory).entryList({f.id+"_*.vcalpart"},QDir::Files,QDir::Name);
    if(parts.isEmpty())return f;
    f.project={}; // Avoid keeping a second native copy while rebuilding the full raw arrays.
    auto merged=f.document();auto project=merged["project"].toObject();
    auto stages=project["stages"].toArray();QMap<QString,QMap<int,QJsonArray>> rows;
    for(const auto& value:stages) {
        const auto stage=value.toObject();
        for(int wave:{816,1368})rows[stage["id"].toString()][wave]=stage["channels"].toObject()[QString::number(wave)].toArray();
    }
    QString previous;quint64 sequence=0,totalBytes=quint64(file.size());
    for(const auto& name:parts) {
        QFile partFile(QDir(partsDirectory).filePath(name));
        if(!partFile.open(QIODevice::ReadOnly) || partFile.size()>256*1024*1024) {
            *error=QStringLiteral("无法读取草稿增量文件：%1").arg(name);return {};
        }
        totalBytes+=partFile.size();
        if(totalBytes>288ULL*1024*1024){*error=QStringLiteral("草稿检查点及增量超过容量边界，未截断任何文件");return {};}
        QJsonParseError parse;auto part=QJsonDocument::fromJson(partFile.readAll(),&parse).object();
        const auto digest=part.take("sha256").toString();
        if(parse.error!=QJsonParseError::NoError || part["kind"]!="vaporview_calibration_progress" ||
            part["schema_version"]!=1 || part["base_file_id"]!=f.id ||
            part["sequence"].toString()!=QString::number(++sequence) || part["previous_sha256"]!=previous ||
            digest.toLatin1()!=calibrationDigest(part)) {
            *error=QStringLiteral("草稿增量损坏、缺段或 SHA-256 不匹配：%1；检查点及原始增量均保留").arg(name);return {};
        }
        auto next=part["project"].toObject();auto nextStages=next["stages"].toArray();
        if(next["id"]!=project["id"] || nextStages.size()!=stages.size()) {
            *error=QStringLiteral("草稿增量任务身份或阶段数量不一致");return {};
        }
        for(int index=0;index<stages.size();++index) {
            const auto stage=nextStages[index].toObject();const auto id=stage["id"].toString();
            if(id!=stages[index].toObject()["id"].toString()) {
                *error=QStringLiteral("草稿增量阶段顺序不一致");return {};
            }
            for(int wave:{816,1368}) {
                auto& values=rows[id][wave];const auto key=QString::number(wave);
                const auto appended=stage["channels"].toObject()[key];
                if(part["base_counts"].toObject()[id].toObject()[key].toInt(-1)!=values.size() || !appended.isArray()) {
                    *error=QStringLiteral("草稿增量起点不一致，拒绝重复累计");return {};
                }
                for(const auto& row:appended.toArray())values.append(row);
            }
        }
        project=next;stages=nextStages;previous=digest;
    }
    for(int index=0;index<stages.size();++index) {
        auto stage=stages[index].toObject();QJsonObject channels;
        for(int wave:{816,1368})channels[QString::number(wave)]=rows[stage["id"].toString()][wave];
        stage["channels"]=channels;stages[index]=stage;
    }
    project["stages"]=stages;merged["project"]=project;merged.remove("sha256");
    merged["sha256"]=QString::fromLatin1(calibrationDigest(merged));
    auto restored=decode(QJsonDocument(merged).toJson(QJsonDocument::Compact),true,error);
    restored.filename=f.filename;return restored;
}
bool CalibrationFileStore::copy(const QString& source,const QString& name,CalibrationFile& out,QString *error) const
{
    const auto f=read(source,false,error);if(!f.valid)return false;
    const auto path=uniqueFilename(name);if(!write(path,f.document(),false,error))return false;
    out=read(path,false,error);return out.valid;
}
bool CalibrationFileStore::remove(const QString& path,const QString& locked,QString *error) const
{
    const auto f=read(path,false,error);if(!f.valid)return false;
    if(!locked.isEmpty() && f.sha256==locked) { *error=QStringLiteral("该定标文件被活动 Session 锁定，请先停止录制");return false; }
    if(QFileInfo(path).absolutePath()!=QFileInfo(directory()).absoluteFilePath() || !QFile::remove(path)) {
        *error=QStringLiteral("无法删除定标目录中的文件：%1").arg(path);return false;
    }return true;
}
bool CalibrationFileStore::archive(const QString& path,const QString& locked,QString *error) const
{
    const auto f=read(path,false,error);if(!f.valid)return false;
    if(!locked.isEmpty() && f.sha256==locked) { *error=QStringLiteral("活动 Session 使用的文件不能归档");return false; }
    const auto target=QDir(directory()).filePath("archive/"+calibrationUuid()+"_"+QFileInfo(path).fileName());
    if(QFileInfo(path).absolutePath()!=QFileInfo(directory()).absoluteFilePath() || !QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::rename(path,target)) {
        *error=ioError(target,QStringLiteral("归档失败"));return false;
    }return true;
}
}
