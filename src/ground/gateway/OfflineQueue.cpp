#include "ground/gateway/OfflineQueue.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <limits>

namespace VaporView::Ground::Gateway
{
namespace
{

bool fail(QString *error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool earlierVictim(const OfflineQueue::Item& left, const OfflineQueue::Item& right)
{
    if (left.priority != right.priority) return left.priority < right.priority;
    if (left.createdMs != right.createdMs) return left.createdMs < right.createdMs;
    return left.id < right.id;
}

QByteArray serialize(const OfflineQueue::Item& item)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("id"), item.id},
        {QStringLiteral("body"), item.body},
        {QStringLiteral("priority"), item.priority},
        {QStringLiteral("created_ms"), QString::number(item.createdMs)},
        {QStringLiteral("expires_ms"), QString::number(item.expiresMs)}
    }).toJson(QJsonDocument::Compact);
}

bool deserialize(const QByteArray& data, OfflineQueue::Item& item)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (object.value(QStringLiteral("version")) != QJsonValue(1) ||
        !object.value(QStringLiteral("id")).isString() ||
        !object.value(QStringLiteral("body")).isObject() ||
        !object.value(QStringLiteral("priority")).isDouble()) return false;
    item.id = object.value(QStringLiteral("id")).toString();
    item.body = object.value(QStringLiteral("body")).toObject();
    const double priority = object.value(QStringLiteral("priority")).toDouble();
    if (priority < std::numeric_limits<int>::min() || priority > std::numeric_limits<int>::max()) return false;
    item.priority = static_cast<int>(priority);
    if (priority != item.priority) return false;
    bool createdOk = false;
    bool expiresOk = false;
    item.createdMs = object.value(QStringLiteral("created_ms")).toString().toLongLong(&createdOk);
    item.expiresMs = object.value(QStringLiteral("expires_ms")).toString().toLongLong(&expiresOk);
    return !item.id.isEmpty() && createdOk && expiresOk && item.createdMs > 0 &&
           (item.expiresMs == 0 || item.expiresMs > item.createdMs);
}

}  // namespace

OfflineQueue::OfflineQueue(QString directory, qint64 maxBytes)
    : directory_(QDir(directory).absolutePath()), max_bytes_(maxBytes)
{
}

QString OfflineQueue::pathForId(const QString& id) const
{
    const auto digest = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directory_).filePath(QString::fromLatin1(digest) + QStringLiteral(".json"));
}

bool OfflineQueue::open(QString *error)
{
    if (error) error->clear();
    opened_ = false;
    entries_.clear();
    bytes_ = 0;
    if (max_bytes_ <= 0 || !QDir().mkpath(directory_))
        return fail(error, QStringLiteral("Cannot create Gateway cache directory or invalid cache limit."));

    const QRegularExpression recordName(QStringLiteral("^[a-f0-9]{64}\\.json$"));
    const auto files = QDir(directory_).entryInfoList(QDir::Files | QDir::System | QDir::Hidden);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const auto& info : files)
    {
        if (!recordName.match(info.fileName()).hasMatch()) continue;
        QFile file(info.absoluteFilePath());
        Item item;
        bool valid = !info.isSymLink() && info.size() <= max_bytes_ && file.open(QIODevice::ReadOnly);
        if (valid)
        {
            valid = deserialize(file.readAll(), item) &&
                    QFileInfo(pathForId(item.id)).fileName() == info.fileName() &&
                    (item.expiresMs == 0 || item.expiresMs > nowMs);
            file.close();
        }
        if (!valid)
        {
            if (!file.remove()) return fail(error, QStringLiteral("Cannot remove invalid Gateway cache record."));
            continue;
        }
        entries_.append({item, info.size()});
        bytes_ += info.size();
    }
    if (!enforceLimit(error)) return false;
    opened_ = true;
    return true;
}

bool OfflineQueue::removeAt(qsizetype index, QString *error)
{
    QFile file(pathForId(entries_.at(index).item.id));
    if (file.exists() && !file.remove())
        return fail(error, QStringLiteral("Cannot remove Gateway cache record: %1").arg(file.errorString()));
    bytes_ -= entries_.at(index).diskBytes;
    entries_.removeAt(index);
    return true;
}

bool OfflineQueue::enforceLimit(QString *error)
{
    while (bytes_ > max_bytes_ && !entries_.isEmpty())
    {
        const auto victim = std::min_element(entries_.cbegin(), entries_.cend(), [](const Entry& a, const Entry& b) {
            return earlierVictim(a.item, b.item);
        });
        if (!removeAt(victim - entries_.cbegin(), error)) return false;
    }
    return true;
}

bool OfflineQueue::enqueue(const Item& value, QString *error)
{
    if (error) error->clear();
    if (!opened_) return fail(error, QStringLiteral("Gateway cache has not been opened."));
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    prune(nowMs);
    if (value.id.isEmpty()) return fail(error, QStringLiteral("Gateway cache item has no ID."));
    for (const auto& entry : entries_)
        if (entry.item.id == value.id) return true;

    Item item = value;
    if (item.createdMs == 0) item.createdMs = nowMs;
    if (item.createdMs < 0 || item.expiresMs < 0 ||
        (item.expiresMs > 0 && (item.expiresMs <= nowMs || item.expiresMs <= item.createdMs)))
        return fail(error, QStringLiteral("Gateway cache item has expired or has invalid timestamps."));
    const QByteArray data = serialize(item);
    if (data.size() > max_bytes_) return fail(error, QStringLiteral("Gateway cache item exceeds capacity."));

    // Do not evict important records merely to admit a lower-priority arrival.
    qint64 evictableBytes = 0;
    for (const auto& entry : entries_)
        if (earlierVictim(entry.item, item)) evictableBytes += entry.diskBytes;
    if (bytes_ + data.size() - evictableBytes > max_bytes_)
        return fail(error, QStringLiteral("Gateway cache capacity is reserved for higher-priority records."));

    QSaveFile file(pathForId(item.id));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail(error, QStringLiteral("Cannot persist Gateway cache record: %1").arg(file.errorString()));
    entries_.append({item, data.size()});
    bytes_ += data.size();
    if (!enforceLimit(error))
    {
        // The failed eviction must not silently admit an over-capacity item.
        for (qsizetype index = 0; index < entries_.size(); ++index)
            if (entries_.at(index).item.id == item.id) { removeAt(index, nullptr); break; }
        return false;
    }
    return true;
}

QList<OfflineQueue::Item> OfflineQueue::peek(int maxItems, qint64 maxPayloadBytes, qint64 nowMs)
{
    prune(nowMs);
    QList<Item> result;
    if (!opened_ || maxItems <= 0 || maxPayloadBytes <= 0) return result;
    QList<Entry> ordered = entries_;
    std::sort(ordered.begin(), ordered.end(), [](const Entry& a, const Entry& b) {
        if (a.item.priority != b.item.priority) return a.item.priority > b.item.priority;
        if (a.item.createdMs != b.item.createdMs) return a.item.createdMs < b.item.createdMs;
        return a.item.id < b.item.id;
    });
    qint64 payloadBytes = 0;
    for (const auto& entry : ordered)
    {
        if (entry.item.expiresMs > 0 && entry.item.expiresMs <= nowMs) continue;
        const auto size = QJsonDocument(entry.item.body).toJson(QJsonDocument::Compact).size();
        if (payloadBytes + size > maxPayloadBytes) continue;
        result.append(entry.item);
        payloadBytes += size;
        if (result.size() >= maxItems) break;
    }
    return result;
}

bool OfflineQueue::remove(const QStringList& ids, QString *error)
{
    if (error) error->clear();
    if (!opened_) return fail(error, QStringLiteral("Gateway cache has not been opened."));
    for (qsizetype index = entries_.size(); index-- > 0;)
        if (ids.contains(entries_.at(index).item.id) && !removeAt(index, error)) return false;
    return true;
}

void OfflineQueue::prune(qint64 nowMs)
{
    for (qsizetype index = entries_.size(); index-- > 0;)
        if (entries_.at(index).item.expiresMs > 0 && entries_.at(index).item.expiresMs <= nowMs)
            removeAt(index, nullptr);
}

qint64 OfflineQueue::bytes() const { return bytes_; }
int OfflineQueue::count() const { return static_cast<int>(entries_.size()); }

}  // namespace VaporView::Ground::Gateway
