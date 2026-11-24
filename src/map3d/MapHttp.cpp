#include "MapHttp.h"

#include <osgEarth/Progress>
#include <QDir>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

namespace VaporView::Map3D::Detail {
namespace {

// All map workers share one HTTP cache. Its files and Qt network objects have
// a single owner, including when two map windows are loading simultaneously.
class MapNetworkThread final : public QThread
{
public:
    MapNetworkThread() { start(); ready_.acquire(); }
    ~MapNetworkThread() override { quit(); wait(); }
    QNetworkAccessManager* network = nullptr;

private:
    void run() override
    {
        QNetworkAccessManager manager;
        const QString root = qEnvironmentVariable("OSGEARTH_CACHE_PATH",
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        const QString directory = QDir(root).filePath(QStringLiteral("map-http"));
        QLockFile lock(QDir(directory).filePath(QStringLiteral("writer.lock")));
        if (!root.isEmpty() && QDir().mkpath(directory) && lock.tryLock())
        {
            auto* cache = new QNetworkDiskCache(&manager);
            cache->setCacheDirectory(directory);
            cache->setMaximumCacheSize(256LL * 1024 * 1024);
            manager.setCache(cache);
        }
        network = &manager;
        ready_.release();
        exec();
        manager.setCache(nullptr);
    }
    QSemaphore ready_;
};

} // namespace

MapHttpResult mapHttpGet(QNetworkRequest request, osgEarth::ProgressCallback* progress)
{
    static MapNetworkThread thread;
    struct Pending {
        MapHttpResult result;
        QSemaphore done;
        std::atomic<bool> canceled{false};
    };
    auto pending = std::make_shared<Pending>();
    request.setRawHeader("User-Agent", "VaporView/1.0 (+https://github.com/Winter-And-You-Gone/VaporView)");
    request.setTransferTimeout(8000);
    auto* manager = thread.network;
    QMetaObject::invokeMethod(manager, [manager, request, pending] {
        auto* reply = manager->get(request);
        auto* timer = new QTimer(reply);
        QObject::connect(timer, &QTimer::timeout, reply, [reply, pending] {
            if (pending->canceled.load()) reply->abort();
        });
        timer->start(100);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, pending] {
            pending->result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            pending->result.fromCache = reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool();
            if (reply->error() == QNetworkReply::NoError)
                pending->result.body = reply->readAll();
            reply->deleteLater();
            pending->done.release();
        });
    });
    while (!pending->done.tryAcquire(1, 100))
    {
        if (progress && progress->isCanceled())
        {
            pending->canceled = true;
            return {};
        }
    }
    return std::move(pending->result);
}

} // namespace VaporView::Map3D::Detail
