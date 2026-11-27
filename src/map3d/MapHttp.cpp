#include "MapHttp.h"
#include "OnlineMapResource.h"

#include <osgEarth/Progress>
#include <osgEarth/TileKey>
#include <QDir>
#include <QImage>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstring>
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
    if (!request.hasRawHeader("User-Agent"))
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

QNetworkRequest credentialedImageryRequest(OnlineMapProvider provider, const OnlineMapResource& resource,
                                          unsigned z, unsigned x, unsigned y)
{
    const bool tianditu = provider == OnlineMapProvider::Tianditu;
    QNetworkRequest request{QUrl(expandTileUrl(resource.requestUrl, z, x, y, tianditu ? resource.credential : QString()))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    // The URI or authorization header contains a credential. Keep it out of the shared HTTP disk cache.
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    if (tianditu)
    {
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                          "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36");
        request.setRawHeader("Referer", "https://map.tianditu.gov.cn/");
    }
    else
        request.setRawHeader("Authorization", "Bearer " + resource.credential.toUtf8());
    return request;
}

osgEarth::GeoImage readCredentialedImageryTile(OnlineMapProvider provider, const OnlineMapResource& resource,
                                              const osgEarth::TileKey& key, osgEarth::ProgressCallback* progress)
{
    const auto request = credentialedImageryRequest(provider, resource, key.getLOD(), key.getTileX(), key.getTileY());
    const auto tile = mapHttpGet(request, progress);
    const QImage image = QImage::fromData(tile.body).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return osgEarth::GeoImage::INVALID;

    osg::ref_ptr<osg::Image> decoded = new osg::Image;
    decoded->allocateImage(image.width(), image.height(), 1, GL_RGBA, GL_UNSIGNED_BYTE);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(decoded->data(0, y), image.constScanLine(image.height() - 1 - y), image.width() * 4);
    return osgEarth::GeoImage(decoded.get(), key.getExtent());
}

} // namespace VaporView::Map3D::Detail
