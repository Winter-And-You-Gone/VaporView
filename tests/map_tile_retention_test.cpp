#include "MapTileRetention.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QThread>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

using namespace VaporView::Map3D::Detail;
namespace {
void require(bool result, const char* message) {
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
QString makeTile(const QTemporaryDir& dir, const char* bin, char id, int day) {
    const QString key = QString::fromLatin1(bin) + "/01/" + QString(38, QLatin1Char(id));
    auto path = dir.filePath(key + ".osgb");
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "create fixture shard");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write("data") == 4, "write fixture");
    file.close();
    require(file.open(QIODevice::ReadWrite) && file.setFileTime(QDateTime::fromSecsSinceEpoch(day * 86400LL),
            QFileDevice::FileModificationTime), "set fixture timestamp");
    return key;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 4) {
        const auto args = app.arguments();
        qputenv("OSGEARTH_CACHE_PATH", args[1].toUtf8());
        qunsetenv("OSGEARTH_CACHE_ONLY");
        qunsetenv("OSGEARTH_NO_CACHE");
        startTileRetention();
        QElapsedTimer deadline;
        deadline.start();
        while (QFile::exists(args[2]) && deadline.elapsed() < 5000) QThread::msleep(5);
        require(!QFile::exists(args[2]), "launch cleans the cache without opening a map");
        // This expired tile arrives after the sweep. Opening the map must not sweep it again.
        require(QFile::copy(args[3], args[2]), "restore expired fixture for next launch");
        QFile restored(args[2]);
        require(restored.open(QIODevice::ReadWrite) && restored.setFileTime(QDateTime::fromSecsSinceEpoch(86400),
            QFileDevice::FileModificationTime), "age restored fixture");
        restored.close();
        startTileRetention();
        recordTileVisit(terrainCacheBin, "01/" + std::string(38, 'a'));
        finishTileRetention();
        require(QFile::exists(args[2]), "opening and closing the map does not rescan");
        return 0;
    }
    QTemporaryDir dir;
    require(dir.isValid(), "isolated cache");
    const auto hot = makeTile(dir, terrainCacheBin, 'a', 100);
    const auto cold = makeTile(dir, osmCacheBin, 'b', 100);
    const auto untouched = makeTile(dir, terrainCacheBin, 'c', 100);
    const auto fresh = makeTile(dir, osmCacheBin, 'd', 124);
    const auto foreign = makeTile(dir, "local-assets", 'e', 10);
    const auto unrelated = makeTile(dir, terrainCacheBin, 'z', 20);
    const auto exists = [&](const QString& key){ return QFile::exists(dir.filePath(key + ".osgb")); };
    const auto stamp = QFileInfo(dir.filePath(hot + ".osgb")).lastModified();
    for (int i = 0; i < 5; ++i) {
        require(maintainTileCache(dir.path(), {{hot,100+i},{cold,100}}, 100+i, false), "persist separate visit days");
    }
    require(maintainTileCache(dir.path(), {}, 130, true), "expire old tiles after reopening index");
    require(!exists(cold) && !exists(untouched) && exists(hot) && exists(fresh) && exists(foreign) && exists(unrelated),
        "frequent tile survives, same-day visits and old legacy tiles expire, foreign files survive");
    require(QFileInfo(dir.filePath(hot + ".osgb")).lastModified() == stamp, "freshness timestamp preserved");
    require(maintainTileCache(dir.path(), {}, 130, true, 4) && exists(hot) && !exists(fresh),
        "under pressure favor recurrent visits across both bins");
    QLockFile other(dir.filePath(".vaporview-retention.lock"));
    require(other.tryLock() && !maintainTileCache(dir.path(), {}, 220, true) && exists(hot), "respect concurrent maintenance");
    other.unlock();
    require(maintainTileCache(dir.path(), {}, 220, true) && !exists(hot) && exists(foreign), "even hot data ages out when abandoned");
    const auto frequent = makeTile(dir, terrainCacheBin, 'f', 220);
    for (int day = 220; day <= 250; ++day)
        require(maintainTileCache(dir.path(), {{frequent, day}}, day, false), "record frequent visits");
    require(maintainTileCache(dir.path(), {}, 339, true) && exists(frequent), "frequent tile survives 89 idle days");
    require(maintainTileCache(dir.path(), {}, 340, true) && !exists(frequent), "retention is capped at 90 idle days");
    QTemporaryDir startupCache;
    require(startupCache.isValid(), "isolated startup cache");
    const int today = int(QDateTime::currentSecsSinceEpoch() / 86400);
    const auto oldPath = startupCache.filePath(makeTile(startupCache, osmCacheBin, 'c', today - 40) + ".osgb");
    const auto recent = makeTile(startupCache, terrainCacheBin, 'a', today);
    for (int launch = 0; launch < 2; ++launch) {
        require(QProcess::execute(app.applicationFilePath(), {startupCache.path(), oldPath,
                    startupCache.filePath(recent + ".osgb")}) == 0, "short session exits cleanly");
    }
    QFile index(startupCache.filePath(".vaporview-retention-v1.json"));
    require(index.open(QIODevice::ReadOnly), "short session flushes visits on exit");
    const auto visit = QJsonDocument::fromJson(index.readAll()).object().value(recent).toArray();
    require(visit.size() == 2 && visit.first().toInteger() >= today, "visit day persisted");
    return 0;
}
