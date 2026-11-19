#include "ground/gateway/GatewayService.h"
#include "ground/gateway/GatewayListener.h"
#include "ground/gateway/OfflineQueue.h"
#include "ground/devices/GroundTelemetryService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace VaporView::Ground::Gateway
{
namespace
{
constexpr qint64 kIngressBytes = 4 * 1024 * 1024;
constexpr qint64 kBatchBytes = 256 * 1024;
constexpr qint64 kResponseBytes = 1024 * 1024;
constexpr qint64 kDayMs = 24 * 60 * 60 * 1000;

QString uniqueId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString hash(const QByteArray& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
qint64 expiry(const QJsonObject& command)
{
    const QString text = command.value("expiresAt").toString();
    const QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    return text.endsWith('Z') && date.isValid() ? date.toMSecsSinceEpoch() : 0;
}
bool sameConfig(const GatewayConfig& a, const GatewayConfig& b)
{
    return a.enabled == b.enabled && a.serverUrl == b.serverUrl && a.deviceId == b.deviceId &&
        a.token == b.token && a.allowRemoteCommands == b.allowRemoteCommands && a.allowedCommands == b.allowedCommands &&
        a.maxCacheBytes == b.maxCacheBytes && a.retentionHours == b.retentionHours && a.cacheDirectory == b.cacheDirectory;
}
bool safeServer(const QUrl& url)
{
    const bool loopback = url.host() == QLatin1String("127.0.0.1") ||
        url.host() == QLatin1String("::1") || url.host() == QLatin1String("localhost");
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
        !url.hasQuery() && !url.hasFragment() &&
        (url.scheme() == QLatin1String("https") || (loopback && url.scheme() == QLatin1String("http")));
}
bool dataFrame(MsgType type)
{
    switch (type)
    {
    case MsgType::TelemetryBasic: case MsgType::WaveformDownsampled: case MsgType::WaveformFeature:
    case MsgType::TelemetryStatus: case MsgType::TemperatureControllerStatus:
    case MsgType::Ai8TemperatureControllerStatus: case MsgType::FpgaStatus:
    case MsgType::FpgaSensor: case MsgType::FpgaWaveformPreview: case MsgType::InternalEnvironment:
    case MsgType::FpgaCycleFeature: case MsgType::Heartbeat: return true;
    default: return false;
    }
}
bool saveJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readJson(const QString& path, qint64 maxBytes = kResponseBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxBytes) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QJsonObject resultFor(const QString& id, const QString& status)
{
    return {{"id", id}, {"kind", "command-result"}, {"status", status},
            {"completedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
}
}  // namespace

struct GatewayIngress
{
    QMutex mutex;
    QList<TelemetryFrame> frames;
    qint64 bytes = 0;
    quint64 dropped = 0;
    quint64 generation = 0;
};

class GatewayWorker final : public QObject
{
public:
    GatewayWorker(GatewayService *owner, std::shared_ptr<GatewayIngress> ingress)
        : owner_(owner), ingress_(std::move(ingress))
    {
        timer_ = new QTimer(this);
        timer_->setInterval(200);
        connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    }

    void configure(const GatewayConfig& config, quint64 generation, std::shared_ptr<GatewayIngress> ingress)
    {
        stop(true);
        ingress_ = std::move(ingress);
        config_ = config;
        generation_ = generation;
        if (!config.enabled) { report(QStringLiteral("Gateway 未启用")); return; }
        const auto badHeader = [](const QString& value) {
            return std::any_of(value.cbegin(), value.cend(), [](QChar ch) { return ch.unicode() < 33 || ch.unicode() > 126; });
        };
        if (!safeServer(config.serverUrl) || config.deviceId.trimmed().isEmpty() || badHeader(config.deviceId) ||
            config.deviceId.size() > 128 || config.token.trimmed().isEmpty() || config.token.contains('\n') ||
            config.token.contains('\r') || badHeader(config.token) || config.maxCacheBytes < 1024 * 1024 || config.retentionHours < 1 ||
            config.retentionHours > 720)
        {
            report(QStringLiteral("Gateway 配置无效：需要 HTTPS 地址、设备 ID、令牌和有效缓存限制"));
            return;
        }
        QString base = config.cacheDirectory;
        if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/gateway";
        // Never upload an old device's backlog to a newly configured endpoint.
        directory_ = QDir(base).filePath(hash(config.serverUrl.toEncoded() + '\n' + config.deviceId.toUtf8()));
        if (!QDir().mkpath(directory_)) { report(QStringLiteral("Gateway 无法创建缓存目录")); return; }
        queue_ = std::make_unique<OfflineQueue>(directory_ + "/queue", config.maxCacheBytes);
        QString error;
        if (!queue_->open(&error)) { queue_.reset(); report(QStringLiteral("Gateway 缓存打开失败：") + error); return; }
        const QString ledgerPath = directory_ + "/commands.json";
        seen_ = readJson(ledgerPath, 8 * kResponseBytes);
        ledgerValid_ = !QFile::exists(ledgerPath) || !seen_.isEmpty();
        for (auto it = seen_.begin(); it != seen_.end(); ++it)
        {
            const auto entry = it.value().toObject();
            bool validUntil = false;
            entry.value("until").toString().toLongLong(&validUntil);
            if (!validUntil || entry.value("id").toString().isEmpty()) ledgerValid_ = false;
        }
        jobs_ = readJson(directory_ + "/sessions.json").value("jobs").toArray();
        manager_ = new QNetworkAccessManager(this);
        nextPoll_ = 0;
        retryAt_ = 0;
        retryMs_ = 1000;
        timer_->start();
        report(QStringLiteral("Gateway 已启动，等待云端确认"));
        if (!ledgerValid_) report(QStringLiteral("Gateway 命令去重记录损坏，远程命令已停用"));
        const auto claims = seen_;
        for (auto it = claims.begin(); ledgerValid_ && it != claims.end(); ++it)
        {
            const auto entry = it.value().toObject();
            if (!entry.value("id").toString().isEmpty() && !entry.contains("result"))
                enqueueResult(resultFor(entry.value("id").toString(), "unknown-after-restart"), generation_);
        }
    }

    void stop(bool flush = false)
    {
        timer_->stop();
        if (flush && queue_) while (drain()) {}
        if (reply_)
        {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort();
            reply_->deleteLater();
            reply_.clear();
        }
        delete manager_;
        manager_ = nullptr;
        queue_.reset();
    }

    void configureListener(const GatewayListenerConfig& config, const QString& deviceId, quint64 generation,
                           std::shared_ptr<GatewayListenerIngress> ingress)
    {
        listener_.reset();
        listenerGeneration_ = generation;
        QString text = QStringLiteral("入站网关未启用");
        quint16 port = 0;
        if (config.enabled)
        {
            listener_ = std::make_unique<GatewayListener>(config, deviceId, std::move(ingress),
                [this, generation](const QJsonObject& command) {
                    const auto owner = owner_;
                    QMetaObject::invokeMethod(owner, [owner, command, generation] {
                        if (owner) owner->receiveCommand(command, generation, true);
                    }, Qt::QueuedConnection);
                });
            const auto error = listener_->start();
            if (!error.isEmpty())
            {
                text = error;
                listener_.reset();
            }
            else
            {
                port = listener_->port();
                text = QStringLiteral("HTTP / WS 正在监听 %1").arg(QUrl(
                    QStringLiteral("http://") + (config.address.contains(':')
                        ? QStringLiteral("[") + config.address + QStringLiteral("]") : config.address) +
                    QStringLiteral(":") + QString::number(port)).toDisplayString());
            }
        }
        const auto owner = owner_;
        QMetaObject::invokeMethod(owner, [owner, text, port, generation] {
            if (!owner || owner->listenerGeneration_ != generation) return;
            owner->listenerStatus_ = text;
            owner->listenerPort_ = port;
            emit owner->listenerStatusChanged(text, port);
        }, Qt::QueuedConnection);
    }

    void stopListener() { listener_.reset(); }

    void enqueueListenerResult(const QJsonObject& result, quint64 generation)
    {
        if (listener_ && generation == listenerGeneration_) listener_->complete(result);
    }

    void enqueueResult(QJsonObject result, quint64 generation)
    {
        if (!queue_ || generation != generation_) return;
        result.insert("deviceId", config_.deviceId);
        const QString commandId = result.value("id").toString();
        const QString key = hash(commandId.toUtf8());
        if (ledgerValid_ && seen_.contains(key))
        {
            auto entry = seen_.value(key).toObject();
            // Repeated cloud delivery resends the actual outcome, never a placeholder.
            if (entry.contains("result")) result = entry.value("result").toObject();
            else
            {
                entry.insert("result", result);
                seen_.insert(key, entry);
                if (!saveJson(directory_ + "/commands.json", seen_))
                    report(QStringLiteral("Gateway 命令结果去重记录保存失败"));
            }
        }
        OfflineQueue::Item item;
        item.id = "result-" + hash(result.value("id").toString().toUtf8());
        item.body = result;
        item.body.insert("id", item.id);
        item.body.insert("commandId", result.value("id"));
        item.priority = 100;
        item.createdMs = now();
        item.expiresMs = item.createdMs + qint64(config_.retentionHours) * 3600000;
        QString error;
        if (!queue_->enqueue(item, &error)) report(QStringLiteral("Gateway 命令结果缓存失败：") + error);
    }

    void enqueueSession(const QString& directory, quint64 generation)
    {
        if (generation != generation_) return;
        if (!queue_) { sessionDone(directory, false, QStringLiteral("请先启用有效的 Gateway 配置")); return; }
        const QString root = QFileInfo(directory).canonicalFilePath();
        const QJsonObject manifest = readJson(QDir(root).filePath("session.json"));
        const QString state = manifest.value("state").toString();
        if (root.isEmpty() || QDir(root).isRoot() || QFileInfo(directory).isSymLink() || QFileInfo(directory).isJunction() ||
            manifest.value("session_format").toString() != QLatin1String("vaporview.session") ||
            manifest.value("session_format_version").toInt() != 1 ||
            (state != QLatin1String("complete") && state != QLatin1String("recovered")))
        {
            sessionDone(directory, false, QStringLiteral("仅支持已结束的地面 Session 目录")); return;
        }
        if (jobs_.size() >= 16) { sessionDone(directory, false, QStringLiteral("待同步 Session 已达 16 个上限")); return; }
        QJsonArray files;
        QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isSymLink() || info.isJunction()) { sessionDone(directory, false, QStringLiteral("Session 不支持符号链接")); return; }
            if (!info.isFile()) continue;
            const QString relative = QDir(root).relativeFilePath(info.absoluteFilePath());
            files.append(QJsonObject{{"path", relative}, {"size", QString::number(info.size())},
                                     {"modified", QString::number(info.lastModified().toMSecsSinceEpoch())}});
            if (files.size() > 4096) { sessionDone(directory, false, QStringLiteral("Session 文件数量超过上限")); return; }
        }
        QJsonObject job{{"directory", root}, {"files", files}, {"index", 0}, {"offset", "0"},
                        {"expires", QString::number(now() + qint64(config_.retentionHours) * 3600000)}};
        job.insert("id", hash(root.toUtf8() + QJsonDocument(files).toJson(QJsonDocument::Compact)));
        for (const auto& value : jobs_)
            if (value.toObject().value("id") == job.value("id")) { report(QStringLiteral("Session 已在同步队列中")); return; }
        jobs_.append(job);
        if (!saveJobs()) { jobs_.removeLast(); sessionDone(directory, false, QStringLiteral("Session 同步任务保存失败")); return; }
        report(QStringLiteral("Session 已排队，断网后将从已确认位置继续"));
    }

private:
    void report(const QString& text)
    {
        const auto owner = owner_;
        const auto generation = generation_;
        QMetaObject::invokeMethod(owner_, [owner, text, generation] {
            if (owner && owner->generation_ == generation)
            {
                emit owner->statusChanged(text);
            }
        }, Qt::QueuedConnection);
    }
    void sessionDone(const QString& directory, bool success, const QString& error)
    {
        const auto owner = owner_;
        QMetaObject::invokeMethod(owner_, [owner, directory, success, error] {
            if (owner) emit owner->sessionSyncFinished(directory, success, error);
        }, Qt::QueuedConnection);
    }
    bool saveJobs()
    {
        const QJsonObject object{{"jobs", jobs_}};
        return QJsonDocument(object).toJson(QJsonDocument::Compact).size() <= kResponseBytes &&
            saveJson(directory_ + "/sessions.json", object);
    }
    bool drain()
    {
        QList<TelemetryFrame> frames;
        quint64 dropped;
        {
            QMutexLocker lock(&ingress_->mutex);
            if (ingress_->generation != generation_) return false;
            qint64 bytes = 0;
            while (!ingress_->frames.isEmpty())
            {
                const auto& frame = ingress_->frames.front();
                if (bytes + frame.payload.size() + 64 > kBatchBytes) break;
                bytes += frame.payload.size() + 64;
                ingress_->bytes -= frame.payload.size() + 64;
                frames.append(ingress_->frames.takeFirst());
            }
            dropped = std::exchange(ingress_->dropped, 0);
        }
        if (dropped) report(QStringLiteral("Gateway 输入队列已满，丢弃 %1 帧；本地接收继续").arg(dropped));
        if (frames.isEmpty()) return false;
        QJsonArray array;
        for (const auto& frame : frames)
            array.append(QJsonObject{{"type", static_cast<int>(frame.type)}, {"flags", frame.flags},
                                     {"sequence", frame.seq}, {"timeUs", QString::number(frame.time_us)},
                                     {"payloadBase64", QString::fromLatin1(frame.payload.toBase64())}});
        OfflineQueue::Item item;
        item.id = uniqueId();
        item.createdMs = now();
        item.expiresMs = item.createdMs + qint64(config_.retentionHours) * 3600000;
        item.body = {{"id", item.id}, {"kind", "telemetry"}, {"deviceId", config_.deviceId},
                     {"receivedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"frames", array}};
        QString error;
        if (!queue_->enqueue(item, &error)) report(QStringLiteral("Gateway 遥测缓存失败：") + error);
        return true;
    }
    using Callback = std::function<void(const QJsonObject&)>;
    void request(const QString& endpoint, const QJsonObject& body, Callback callback, bool get = false)
    {
        QUrl base = config_.serverUrl;
        QString path = base.path();
        if (!path.endsWith('/')) path += '/';
        base.setPath(path + "gateway/v1/" + endpoint);
        if (get) { QUrlQuery query; query.addQueryItem("deviceId", config_.deviceId); base.setQuery(query); }
        QNetworkRequest request(base);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + config_.token.toUtf8());
        request.setRawHeader("X-VaporView-Device-Id", config_.deviceId.toUtf8());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(15000);
        reply_ = get ? manager_->get(request) : manager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QNetworkReply *reply = reply_;
        auto bytes = std::make_shared<QByteArray>();
        connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
            const qint64 room = kResponseBytes - bytes->size();
            bytes->append(reply->read(room + 1));
            if (bytes->size() > kResponseBytes || reply->bytesAvailable() > 0) reply->abort();
        });
        QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, get, callback = std::move(callback)] {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QJsonParseError error;
            const QJsonDocument json = QJsonDocument::fromJson(*bytes, &error);
            const bool ok = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300 &&
                bytes->size() <= kResponseBytes && error.error == QJsonParseError::NoError && json.isObject();
            reply_.clear();
            reply->deleteLater();
            if (!ok)
            {
                if (get) { nextPoll_ = now() + pollRetryMs_; pollRetryMs_ = std::min(60000, pollRetryMs_ * 2); }
                else { retryAt_ = now() + retryMs_; retryMs_ = std::min(60000, retryMs_ * 2); }
                report(QStringLiteral("Gateway 云端未确认（HTTP %1），缓存后重试").arg(code));
                return;
            }
            if (get) pollRetryMs_ = 5000; else retryMs_ = 1000;
            callback(json.object());
        });
    }
    void tick()
    {
        if (!queue_ || !manager_) return;
        drain();
        if (reply_ || now() < retryAt_) return;
        if (now() >= nextPoll_)
        {
            nextPoll_ = now() + 5000;
            request("commands", {}, [this](const QJsonObject& json) { commands(json.value("commands").toArray()); }, true);
            return;
        }
        if (sessionTurn_ && !jobs_.isEmpty()) { sessionTurn_ = false; uploadSession(); return; }
        sessionTurn_ = true;
        const auto items = queue_->peek(64, 512 * 1024);
        if (items.isEmpty()) { if (!jobs_.isEmpty()) uploadSession(); return; }
        const bool results = items.front().priority > 0;
        QJsonArray array;
        QStringList sent;
        for (const auto& item : items)
            if ((item.priority > 0) == results) { array.append(item.body); sent.append(item.id); }
        request(results ? "command-results" : "telemetry", {{"deviceId", config_.deviceId}, {"items", array}},
                [this, sent](const QJsonObject& response) {
            QStringList accepted;
            for (const auto& value : response.value("acceptedIds").toArray())
                if (sent.contains(value.toString())) accepted.append(value.toString());
            QString error;
            if (!queue_->remove(accepted, &error)) report(QStringLiteral("Gateway 缓存确认保存失败：") + error);
            else if (accepted.isEmpty()) { retryAt_ = now() + 5000; report(QStringLiteral("Gateway 等待服务端确认数据 ID")); }
            else report(QStringLiteral("Gateway 在线，待上传 %1 批（%2 KiB）").arg(queue_->count()).arg(queue_->bytes() / 1024));
        });
    }
    void commands(const QJsonArray& array)
    {
        if (!ledgerValid_) return;
        // Keep unexpired claims across restarts. Never evict a live command ID.
        const qint64 current = now();
        for (auto it = seen_.begin(); it != seen_.end();)
            if (it.value().toObject().value("until").toString().toLongLong() < current) it = seen_.erase(it); else ++it;
        int processed = 0;
        for (const auto& value : array)
        {
            if (++processed > 32) break;
            const QJsonObject command = value.toObject();
            const QString id = command.value("id").toString();
            if (id.isEmpty() || id.size() > 128) continue;
            const QString key = hash(id.toUtf8());
            const qint64 expires = expiry(command);
            if (seen_.contains(key))
            {
                const auto result = seen_.value(key).toObject().value("result").toObject();
                if (!result.isEmpty()) enqueueResult(result, generation_);
                continue;
            }
            if (seen_.size() >= 4096) { report(QStringLiteral("Gateway 命令去重记录已满，暂停接收新命令")); continue; }
            seen_.insert(key, QJsonObject{{"until", QString::number(current + 2 * kDayMs)}, {"id", id}});
            if (!saveJson(directory_ + "/commands.json", seen_))
            {
                seen_.remove(key);
                report(QStringLiteral("Gateway 无法持久化命令去重记录，命令未执行"));
                continue;
            }
            if (expires <= current || expires > current + kDayMs)
            { enqueueResult(resultFor(id, "expired"), generation_); continue; }
            const auto owner = owner_;
            const auto generation = generation_;
            QMetaObject::invokeMethod(owner_, [owner, command, generation] {
                if (owner) owner->receiveCommand(command, generation);
            }, Qt::QueuedConnection);
        }
    }
    void failSession(const QJsonObject& job, const QString& error)
    {
        jobs_.removeFirst();
        saveJobs();
        sessionDone(job.value("directory").toString(), false, error);
    }
    void uploadSession()
    {
        QJsonObject job = jobs_.first().toObject();
        const QString root = job.value("directory").toString();
        if (job.value("expires").toString().toLongLong() <= now())
        { failSession(job, QStringLiteral("Session 同步任务已过期")); return; }
        const QJsonArray files = job.value("files").toArray();
        const int index = job.value("index").toInt();
        const bool complete = index == files.size();
        if (index < 0 || index > files.size()) { failSession(job, QStringLiteral("Session 游标无效")); return; }
        QJsonObject body{{"deviceId", config_.deviceId}, {"sessionId", job.value("id")}, {"complete", complete}};
        qint64 nextOffset = 0;
        bool fileComplete = false;
        if (complete) body.insert("files", files);
        else
        {
            const QJsonObject entry = files[index].toObject();
            const QString relative = entry.value("path").toString();
            const QString path = QDir(root).filePath(relative);
            const QFileInfo info(path);
            const qint64 total = entry.value("size").toString().toLongLong();
            const qint64 offset = job.value("offset").toString().toLongLong();
            const QString canonical = info.canonicalFilePath();
            if (QDir::isAbsolutePath(relative) || QDir::cleanPath(relative).startsWith("../") ||
                info.isSymLink() || !canonical.startsWith(QDir(root).canonicalPath() + '/', Qt::CaseInsensitive) ||
                !info.isFile() || info.size() != total || offset < 0 || offset > total ||
                info.lastModified().toMSecsSinceEpoch() != entry.value("modified").toString().toLongLong())
            { failSession(job, QStringLiteral("Session 文件已改变、缺失或路径无效")); return; }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || !file.seek(offset))
            { failSession(job, QStringLiteral("无法读取 Session 文件")); return; }
            const QByteArray data = file.read(kBatchBytes);
            if (data.isEmpty() && offset != total) { failSession(job, QStringLiteral("Session 文件读取失败")); return; }
            nextOffset = offset + data.size();
            fileComplete = nextOffset == total;
            body.insert("relativePath", relative);
            body.insert("offset", QString::number(offset));
            body.insert("totalBytes", QString::number(total));
            body.insert("dataBase64", QString::fromLatin1(data.toBase64()));
            body.insert("chunkSha256", hash(data));
        }
        request("sessions", body, [this, job, complete, nextOffset, fileComplete](const QJsonObject& response) mutable {
            if (!response.value("accepted").toBool() ||
                (!complete && response.value("nextOffset").toVariant().toLongLong() != nextOffset))
            { retryAt_ = now() + 5000; report(QStringLiteral("Gateway 等待 Session 分块确认")); return; }
            if (complete)
            {
                jobs_.removeFirst();
                if (saveJobs()) sessionDone(job.value("directory").toString(), true, {});
                else { jobs_.prepend(job); report(QStringLiteral("Session 完成游标保存失败，将重试确认")); }
            }
            else
            {
                QJsonObject next = job;
                next.insert("index", next.value("index").toInt() + (fileComplete ? 1 : 0));
                next.insert("offset", QString::number(fileComplete ? 0 : nextOffset));
                jobs_[0] = next;
                if (!saveJobs()) { jobs_[0] = job; retryAt_ = now() + 5000; report(QStringLiteral("Session 游标保存失败")); }
            }
        });
    }

    QPointer<GatewayService> owner_;
    std::shared_ptr<GatewayIngress> ingress_;
    QTimer *timer_ = nullptr;
    QNetworkAccessManager *manager_ = nullptr;
    QPointer<QNetworkReply> reply_;
    GatewayConfig config_;
    quint64 generation_ = 0;
    std::unique_ptr<OfflineQueue> queue_;
    QString directory_;
    QJsonObject seen_;
    QJsonArray jobs_;
    qint64 nextPoll_ = 0;
    qint64 retryAt_ = 0;
    int retryMs_ = 1000;
    bool sessionTurn_ = false;
    bool ledgerValid_ = true;
    int pollRetryMs_ = 5000;
    quint64 listenerGeneration_ = 0;
    std::unique_ptr<GatewayListener> listener_;
};

GatewayService::GatewayService(QObject *parent) : QObject(parent), ingress_(std::make_shared<GatewayIngress>())
{
    worker_ = new GatewayWorker(this, ingress_);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &GatewayService::statusChanged, this, [this](const QString& text) { status_ = text; });
    thread_.setObjectName(QStringLiteral("VaporViewGateway"));
    thread_.start();
}

GatewayService::~GatewayService()
{
    for (const auto& connection : connections_) disconnect(connection);
    QMetaObject::invokeMethod(worker_, [this] {
        worker_->stopListener();
        worker_->stop(true);
    }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void GatewayService::configure(const GatewayConfig& config)
{
    const bool outboundChanged = !sameConfig(config_, config);
    const bool listenerChanged = !(config_.listener == config.listener) ||
        (config.listener.enabled && (config_.deviceId != config.deviceId || listenerPort_ == 0));
    for (auto sequence : pending_.keys())
    {
        const auto& pending = pending_[sequence];
        if (pending.listener ? listenerChanged : outboundChanged)
            completeCommand(sequence, "configuration-changed");
    }
    config_ = config;
    if (listenerChanged)
    {
        listenerPort_ = 0;
        listenerIngress_ = std::make_shared<GatewayListenerIngress>();
        listenerIngress_->setConnected(service_ && service_->isOpen());
        const auto generation = ++listenerGeneration_;
        const auto ingress = listenerIngress_;
        QMetaObject::invokeMethod(worker_, [this, config, generation, ingress] {
            worker_->configureListener(config.listener, config.deviceId, generation, ingress);
        }, Qt::QueuedConnection);
    }
    if (outboundChanged)
    {
        ingress_ = std::make_shared<GatewayIngress>();
        ingress_->generation = ++generation_;
        const auto generation = generation_;
        const auto ingress = ingress_;
        QMetaObject::invokeMethod(worker_, [this, config, generation, ingress] {
            worker_->configure(config, generation, ingress);
        }, Qt::QueuedConnection);
    }
}

void GatewayService::setListenerCommandPreparation(ListenerCommandPreparation preparation)
{
    listenerPreparation_ = std::move(preparation);
}

void GatewayService::attach(GroundTelemetryService *service)
{
    for (const auto& connection : connections_) disconnect(connection);
    connections_.clear();
    for (auto sequence : pending_.keys()) completeCommand(sequence, "disconnected");
    service_ = service;
    if (listenerIngress_) listenerIngress_->setConnected(service && service->isOpen());
    if (!service) return;
    connections_.append(connect(service, &GroundTelemetryService::frameReceived, this, [this](const TelemetryFrame& frame) {
        if (config_.listener.enabled && listenerIngress_) listenerIngress_->enqueue(frame);
        if (!config_.enabled || !dataFrame(frame.type)) return;
        QMutexLocker lock(&ingress_->mutex);
        const qint64 size = frame.payload.size() + 64;
        if (size > kBatchBytes || ingress_->bytes + size > kIngressBytes) { ++ingress_->dropped; return; }
        ingress_->frames.append(frame);
        ingress_->bytes += size;
    }));
    connections_.append(connect(service, &GroundTelemetryService::commandAckReceived, this, [this](const CommandAck& ack) {
        const auto found = pending_.constFind(ack.command_seq);
        if (found != pending_.cend() && found->command == ack.command_id)
            completeCommand(ack.command_seq, ack.error_code == CommandErrorCode::Ok ? "acknowledged" : "rejected", &ack);
    }));
    connections_.append(connect(service, &GroundTelemetryService::commandTimedOut, this, [this](CommandId command, quint16 sequence) {
        const auto found = pending_.constFind(sequence);
        if (found != pending_.cend() && found->command == command) completeCommand(sequence, "timeout");
    }));
    connections_.append(connect(service, &GroundTelemetryService::linkOpenChanged, this, [this](bool open) {
        if (listenerIngress_) listenerIngress_->setConnected(open);
        if (open) return;
        const auto sequences = pending_.keys();
        for (auto sequence : sequences) completeCommand(sequence, "disconnected");
    }));
    connections_.append(connect(service, &QObject::destroyed, this, [this] {
        if (listenerIngress_) listenerIngress_->setConnected(false);
        for (auto sequence : pending_.keys()) completeCommand(sequence, "disconnected");
    }));
}

void GatewayService::postResult(const QJsonObject& result, bool listener)
{
    const auto generation = listener ? listenerGeneration_ : generation_;
    QMetaObject::invokeMethod(worker_, [this, result, generation, listener] {
        if (listener) worker_->enqueueListenerResult(result, generation);
        else worker_->enqueueResult(result, generation);
    }, Qt::QueuedConnection);
}

void GatewayService::receiveCommand(const QJsonObject& command, quint64 generation, bool listener)
{
    if (listener)
    {
        if (generation != listenerGeneration_ || !config_.listener.enabled) return;
    }
    else if (generation != generation_ || !config_.enabled) return;
    const QString id = command.value("id").toString();
    const double rawCommand = command.value("commandId").toDouble(-1);
    const int commandId = rawCommand >= 0 && rawCommand <= 65535 && std::floor(rawCommand) == rawCommand
        ? static_cast<int>(rawCommand) : -1;
    const qint64 expires = expiry(command);
    QString rejected;
    const bool allowed = listener ? isListenerCommand(commandId)
        : config_.allowRemoteCommands && commandId >= 0 && config_.allowedCommands.contains(quint16(commandId));
    if (!allowed) rejected = "forbidden";
    else if (!listener && command.value("deviceId").toString() != config_.deviceId) rejected = "wrong-device";
    else if (expires <= now() || expires > now() + kDayMs) rejected = "expired";
    else if (!service_ || !service_->isOpen()) rejected = "disconnected";
    else if (pending_.size() >= 32) rejected = "busy";
    const QByteArray encoded = command.value("payloadBase64").toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > 64 * 1024) rejected = "invalid-payload";
    const auto type = static_cast<CommandId>(commandId);
    auto payload = decoded.decoded;
    if (rejected.isEmpty() && listener && listenerPreparation_)
        rejected = listenerPreparation_(type, payload);
    if (!rejected.isEmpty()) { postResult(resultFor(id, rejected), listener); return; }
    const auto linkGeneration = service_->linkGeneration();
    const quint16 sequence = service_->sendCommand(type, payload, nullptr, expires);
    if (!sequence)
    {
        const bool sameLink = service_ && service_->linkGeneration() == linkGeneration;
        if (sameLink && listener && type == CommandId::StartRecording && !payload.isEmpty()) emit listenerStartNotSent();
        postResult(resultFor(id, listener && !sameLink ? "disconnected" : "not-sent"), listener);
        return;
    }
    pending_.insert(sequence, {id, type, generation, listener});
}

void GatewayService::completeCommand(quint16 sequence, const QString& status, const CommandAck *ack)
{
    const auto found = pending_.find(sequence);
    if (found == pending_.end()) return;
    const Pending pending = *found;
    pending_.erase(found);
    if (pending.generation != (pending.listener ? listenerGeneration_ : generation_)) return;
    QJsonObject result = resultFor(pending.id, status);
    result.insert("sequence", sequence);
    result.insert("skyCommandId", static_cast<int>(pending.command));
    if (ack)
    {
        result.insert("errorCode", static_cast<int>(ack->error_code));
        result.insert("result", ack->result);
        if (pending.listener)
        {
            if (!ack->correlation_id.isEmpty()) result.insert("correlationId", ack->correlation_id);
            if (!ack->session_id.isEmpty()) result.insert("sessionId", ack->session_id);
        }
    }
    postResult(result, pending.listener);
}

void GatewayService::enqueueSession(const QString& directory)
{
    const auto generation = generation_;
    QMetaObject::invokeMethod(worker_, [this, directory, generation] { worker_->enqueueSession(directory, generation); }, Qt::QueuedConnection);
}

}  // namespace VaporView::Ground::Gateway
