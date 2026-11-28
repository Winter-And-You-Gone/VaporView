#include "MapTileRetention.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace VaporView::Map3D::Detail {
namespace {
constexpr qint64 daySeconds = 86400;
constexpr qint64 coldDays = 30;
constexpr qint64 maxDays = 90;

bool linked(const QFileInfo& file) { return file.isSymLink() || file.isJunction(); }

struct Tile {
    QString key;
    qint64 expiry, lastVisit, size, modifiedMs;
};

class RetentionWorker {
public:
    void start(const QString& root) {
        std::lock_guard guard(mutex_);
        if (started_ || root.isEmpty()) return;
        started_ = true;
        root_ = root;
        launch(true);
    }
    void visit(const QString& path) {
        std::lock_guard guard(mutex_);
        if (root_.isEmpty()) return;
        visits_.insert(path, QDateTime::currentSecsSinceEpoch() / daySeconds);
        if (Clock::now() >= nextFlush_ && !job_->isRunning()) launch(false);
    }
    void finish() {
        std::unique_lock lock(mutex_);
        if (root_.isEmpty()) return;
        QString root;
        root.swap(root_); // Stop admitting visits before joining the last writer.
        auto job = std::move(job_);
        lock.unlock();
        job->requestInterruption();
        job->wait();
        if (!visits_.isEmpty()) maintainTileCache(root, visits_, QDateTime::currentSecsSinceEpoch() / daySeconds, false);
        visits_.clear();
    }
    ~RetentionWorker() { finish(); }
private:
    using Clock = std::chrono::steady_clock;
    void launch(bool sweep) { // Called under mutex_, after the preceding job has finished.
        QHash<QString, qint64> batch;
        batch.swap(visits_);
        nextFlush_ = Clock::now() + std::chrono::minutes(2);
        job_.reset(QThread::create([this, root = root_, batch = std::move(batch), sweep] {
            if (!maintainTileCache(root, batch, QDateTime::currentSecsSinceEpoch() / daySeconds, sweep) && !batch.isEmpty()) {
                std::lock_guard guard(mutex_);
                for (auto it = batch.cbegin(); it != batch.cend(); ++it)
                    visits_[it.key()] = std::max(visits_.value(it.key()), it.value());
            }
        }));
        job_->start(QThread::LowPriority);
    }
    std::mutex mutex_;
    bool started_ = false;
    QString root_;
    QHash<QString, qint64> visits_;
    Clock::time_point nextFlush_;
    std::unique_ptr<QThread> job_;
};
RetentionWorker& worker() { static RetentionWorker instance; return instance; }
}

QString mapTileCacheRoot() {
    if (qEnvironmentVariableIsSet("OSGEARTH_CACHE_PATH")) {
        const auto path = qEnvironmentVariable("OSGEARTH_CACHE_PATH");
        return path.isEmpty() ? QString{} : QDir(path).absolutePath();
    }
    const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return base.isEmpty() ? QString{} : QDir(base).filePath(QStringLiteral("VaporView/osgearth"));
}

void startTileRetention() {
    if (!qEnvironmentVariableIsSet("OSGEARTH_CACHE_ONLY") && !qEnvironmentVariableIsSet("OSGEARTH_NO_CACHE"))
        worker().start(mapTileCacheRoot());
}
void finishTileRetention() { worker().finish(); }
void recordTileVisit(const char* bin, const std::string& key) {
    worker().visit(QLatin1String(bin) + '/' + QString::fromStdString(key));
}

bool maintainTileCache(const QString& root, const QHash<QString, qint64>& visits,
                       qint64 today, bool sweep, qint64 budget)
{
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || !QFileInfo(root).isDir() || linked(QFileInfo(root))) return false;
    const QDir directory(QFileInfo(root).canonicalFilePath());
    QLockFile lock(directory.filePath(QStringLiteral(".vaporview-retention.lock")));
    if (!lock.tryLock()) return false;
    const QString indexPath = directory.filePath(QStringLiteral(".vaporview-retention-v1.json"));
    QFile input(indexPath);
    if (linked(QFileInfo(input))) return false;
    QJsonObject index;
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly)) return false;
        index = QJsonDocument::fromJson(input.readAll()).object();
        input.close();
    }
    bool dirty = false;
    for (auto it = visits.cbegin(); it != visits.cend(); ++it) {
        const auto old = index.value(it.key()).toArray();
        const auto day = it.value();
        if (old.size() == 2 && old.at(0).toInteger() >= day) continue;
        // Each distinct visit day adds a week of credit; idle days consume that credit.
        const auto expiry = std::clamp(old.size() == 2 ? old.at(1).toInteger() + 7 : day + coldDays,
                                       day + coldDays, day + maxDays);
        index.insert(it.key(), QJsonArray{day, expiry});
        dirty = true;
    }
    if (sweep) {
        std::vector<Tile> tiles;
        qint64 total = 0;
        QJsonObject surviving;
        const QRegularExpression shardName(QStringLiteral("^[0-9a-f]{2}$"));
        const QRegularExpression tileName(QStringLiteral("^[0-9a-f]{38}\\.osgb$"));
        for (const auto* bin : {terrainCacheBin, osmCacheBin}) {
            const QDir binDir(directory.filePath(QLatin1String(bin)));
            if (linked(QFileInfo(binDir.path()))) continue;
            for (const auto& shard : binDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
                if (QThread::currentThread()->isInterruptionRequested()) return false;
                if (linked(shard) || !shardName.match(shard.fileName()).hasMatch()) continue;
                for (const auto& file : QDir(shard.filePath()).entryInfoList({"*.osgb"}, QDir::Files | QDir::NoSymLinks)) {
                    if (linked(file) || !tileName.match(file.fileName()).hasMatch()) continue;
                    const QString key = QLatin1String(bin) + '/' + shard.fileName() + '/' + file.completeBaseName();
                    const auto stamp = file.lastModified().toMSecsSinceEpoch();
                    const auto written = stamp / (1000 * daySeconds);
                    const auto entry = index.value(key).toArray();
                    qint64 last = written, expiry = written + coldDays;
                    if (entry.size() == 2) {
                        surviving.insert(key, entry);
                        last = std::max(last, entry.at(0).toInteger());
                        expiry = std::max(expiry, entry.at(1).toInteger());
                    }
                    tiles.push_back({key, expiry, last, file.size(), stamp});
                    total += file.size();
                }
            }
        }
        std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) {
            return std::tie(a.expiry, a.lastVisit, a.key) < std::tie(b.expiry, b.lastVisit, b.key);
        });
        for (const auto& tile : tiles) {
            if (QThread::currentThread()->isInterruptionRequested()) return false;
            if (tile.expiry > today && total <= budget) break;
            const QFileInfo file(directory.filePath(tile.key + QStringLiteral(".osgb")));
            // Re-check confinement and the write timestamp after the directory scan.
            if (file.canonicalFilePath() != directory.canonicalPath() + '/' + tile.key + QStringLiteral(".osgb")
                || file.lastModified().toMSecsSinceEpoch() != tile.modifiedMs) continue;
            if (QFile::remove(file.filePath())) {
                total -= tile.size;
                surviving.remove(tile.key);
            }
        }
        dirty |= surviving != index;
        index.swap(surviving);
    }
    if (!dirty) return true;
    QSaveFile out(indexPath);
    if (!out.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(index).toJson(QJsonDocument::Compact);
    return out.write(bytes) == bytes.size() && out.commit();
}
}
