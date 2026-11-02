#include "CalibrationFile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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
bool CalibrationFileStore::write(const QString& path,const QJsonObject& j,bool replace,QString *error) const
{
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) { *error=ioError(path,QStringLiteral("目录创建失败"));return false; }
    QLockFile lock(QDir(directory()).filePath(".write.lock"));
    if(!lock.tryLock(0)) { *error=QStringLiteral("另一个进程正在写定标文件，请稍后重试");return false; }
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
    const auto e=p.validate();if(!e.isEmpty()) { *error=e;return false; }
    return write(QDir(draftsDirectory()).filePath("task_"+p.id+".vcaldraft"),document(p,"vaporview_calibration_draft"),true,error);
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
CalibrationFile CalibrationFileStore::decode(const QByteArray& bytes,bool draft,QString *error)
{
    error->clear();CalibrationFile f;QJsonParseError parse;
    if(bytes.size()>256*1024*1024) { *error=QStringLiteral("定标文件超过 256 MiB 限制");return f; }
    auto doc=QJsonDocument::fromJson(bytes,&parse);auto j=doc.object();const auto hash=j.take("sha256").toString();
    if(parse.error!=QJsonParseError::NoError || !doc.isObject() || j["schema_version"]!=1 ||
       j["kind"]!=(draft?"vaporview_calibration_draft":"vaporview_calibration") || hash.toLatin1()!=calibrationDigest(j) ||
       QUuid(j["file_id"].toString()).isNull() || !QDateTime::fromString(j["created_utc"].toString(),Qt::ISODateWithMs).isValid()) { *error=QStringLiteral("定标文件损坏、SHA-256 不匹配或 schema 不兼容");return f; }
    f.project=CalibrationProject::fromJson(j["project"].toObject(),error);if(!error->isEmpty())return f;
    *error=f.project.validate(!draft);if(!error->isEmpty())return f;
    f.id=j["file_id"].toString();f.createdUtc=j["created_utc"].toString();f.document=doc.object();
    f.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());f.valid=true;return f;
}
CalibrationFile CalibrationFileStore::read(const QString& path,bool draft,QString *error) const
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly)) { *error=QStringLiteral("无法读取定标文件 %1：%2").arg(path,file.errorString());return {}; }
    if(file.size()>256*1024*1024) { *error=QStringLiteral("定标文件过大");return {}; }
    auto f=decode(file.readAll(),draft,error);f.filename=QFileInfo(path).absoluteFilePath();return f;
}
bool CalibrationFileStore::copy(const QString& source,const QString& name,CalibrationFile& out,QString *error) const
{
    const auto f=read(source,false,error);if(!f.valid)return false;
    const auto path=uniqueFilename(name);if(!write(path,f.document,false,error))return false;
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
