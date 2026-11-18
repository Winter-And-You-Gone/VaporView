#include "ground/gateway/OfflineQueue.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

using VaporView::Ground::Gateway::OfflineQueue;

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

OfflineQueue::Item item(const QString& id, int priority = 0, qint64 ageMs = 1000)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return {id, QJsonObject{{QStringLiteral("kind"), QStringLiteral("telemetry")},
                           {QStringLiteral("data"), QString(128, QLatin1Char('x'))}},
            priority, now - ageMs, now + 60000};
}

QString recordPath(const QString& directory, const QString& id)
{
    const auto name = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directory).filePath(QString::fromLatin1(name) + QStringLiteral(".json"));
}

bool containsId(const QList<OfflineQueue::Item>& items, const QString& id)
{
    for (const auto& entry : items)
        if (entry.id == id) return true;
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "isolated queue directory");
    const QString directory = temporary.filePath(QStringLiteral("queue"));
    QString error;
    OfflineQueue queue(directory);
    require(!queue.enqueue(item(QStringLiteral("before-open")), &error) && !error.isEmpty(),
            "enqueue requires a successful open");
    require(queue.open(&error) && error.isEmpty(), "open new cache");

    const QString unsafeId = QStringLiteral("../../outside.json");
    auto first = item(unsafeId);
    require(queue.enqueue(first, &error), "IDs containing path components are safe");
    require(QFile::exists(recordPath(directory, unsafeId)), "ID is stored with a hash filename");
    require(!QFile::exists(temporary.filePath(QStringLiteral("outside.json"))), "ID never becomes an output path");
    const qint64 firstBytes = queue.bytes();
    auto duplicate = first;
    duplicate.body.insert(QStringLiteral("data"), QStringLiteral("replacement"));
    require(queue.enqueue(duplicate, &error), "duplicate enqueue is idempotent");
    require(queue.bytes() == firstBytes && queue.count() == 1 &&
            queue.peek().first().body == first.body, "duplicate retains original durable record");
    require(queue.enqueue(item(QStringLiteral("another"), 100), &error), "enqueue important result");
    require(queue.peek().first().id == QStringLiteral("another"), "peek returns high priority first");

    OfflineQueue recovered(directory);
    require(recovered.open(&error) && recovered.count() == 2 && recovered.bytes() == queue.bytes(),
            "restart recovers pending records and actual disk byte count");
    require(recovered.peek().last().id == unsafeId, "restart preserves external ID exactly");
    require(recovered.remove({QStringLiteral("another"), QStringLiteral("unknown")}, &error),
            "ACK deletion handles missing IDs idempotently");
    require(recovered.count() == 1 && !QFile::exists(recordPath(directory, QStringLiteral("another"))),
            "ACK deletion removes only acknowledged records");
    require(recovered.remove({unsafeId}, &error) && recovered.bytes() == 0, "ACK removes hashed unsafe ID");

    auto expiring = item(QStringLiteral("expiring"));
    const qint64 expiry = QDateTime::currentMSecsSinceEpoch() + 10000;
    expiring.expiresMs = expiry;
    require(recovered.enqueue(expiring), "enqueue TTL record");
    require(recovered.peek(10, 1024 * 1024, expiry).isEmpty() && recovered.count() == 0 &&
            recovered.bytes() == 0 && !QFile::exists(recordPath(directory, expiring.id)),
            "expired records are removed from disk and transmission");
    expiring.expiresMs = QDateTime::currentMSecsSinceEpoch() - 1;
    require(!recovered.enqueue(expiring), "already expired arrivals are rejected");

    auto corrupt = item(QStringLiteral("corrupt"));
    require(recovered.enqueue(corrupt), "seed corrupt-file recovery");
    QFile damaged(recordPath(directory, corrupt.id));
    require(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate), "open a record for damage simulation");
    damaged.write("{broken");
    damaged.close();
    QFile unrelated(QDir(directory).filePath(QStringLiteral("notes.txt")));
    require(unrelated.open(QIODevice::WriteOnly), "create unrelated sentinel");
    unrelated.write("keep");
    unrelated.close();
    OfflineQueue repaired(directory);
    require(repaired.open(&error) && repaired.count() == 0 && repaired.bytes() == 0,
            "invalid JSON is discarded on recovery");
    require(!damaged.exists() && unrelated.exists(), "recovery only removes cache record filenames");

    const QString measuredDirectory = temporary.filePath(QStringLiteral("measure"));
    OfflineQueue measured(measuredDirectory);
    require(measured.open(), "open size measurement cache");
    auto lowA = item(QStringLiteral("low-a"), 0, 4000);
    auto lowB = item(QStringLiteral("low-b"), 0, 3000);
    auto high = item(QStringLiteral("high1"), 9, 2000);
    require(measured.enqueue(lowA), "measure serialized record size");
    const qint64 recordBytes = measured.bytes();
    const QString cappedDirectory = temporary.filePath(QStringLiteral("capped"));
    OfflineQueue capped(cappedDirectory, recordBytes * 2);
    require(capped.open() && capped.enqueue(lowA) && capped.enqueue(lowB), "fill bounded queue");
    require(capped.enqueue(high), "high priority arrival evicts lower-priority oldest item");
    auto retained = capped.peek();
    require(retained.size() == 2 && containsId(retained, high.id) && containsId(retained, lowB.id) &&
            !containsId(retained, lowA.id) && capped.bytes() <= recordBytes * 2,
            "capacity eviction preserves higher priority and more recent low-priority item");
    require(!QFile::exists(recordPath(cappedDirectory, lowA.id)), "evicted record is deleted from disk");
    qint64 actualBytes = 0;
    for (const auto& info : QDir(cappedDirectory).entryInfoList(QDir::Files)) actualBytes += info.size();
    require(capped.bytes() == actualBytes, "limit measures all serialized record bytes");

    OfflineQueue reduced(cappedDirectory, recordBytes);
    require(reduced.open() && reduced.count() == 1 && reduced.peek().first().id == high.id,
            "restart applies a reduced limit by preserving high-priority records");
    auto unimportant = item(QStringLiteral("newer"), 0, 1000);
    require(!reduced.enqueue(unimportant) && reduced.count() == 1 && reduced.peek().first().id == high.id,
            "low-priority arrivals cannot displace important pending results");
    auto tooLarge = item(QStringLiteral("large"), 100);
    tooLarge.body.insert(QStringLiteral("data"), QString(static_cast<int>(recordBytes * 2), QLatin1Char('x')));
    require(!reduced.enqueue(tooLarge) && reduced.count() == 1 && reduced.bytes() <= recordBytes,
            "oversized records fail without modifying existing data");

    const auto payloadBytes = QJsonDocument(high.body).toJson(QJsonDocument::Compact).size();
    require(reduced.peek(1, payloadBytes - 1).isEmpty(), "peek honors upload payload budget");
    require(reduced.peek(1, payloadBytes).size() == 1 && reduced.peek(0).isEmpty(),
            "peek supports exact budget and item count limit");

    OfflineQueue invalidLimit(temporary.filePath(QStringLiteral("invalid")), 0);
    require(!invalidLimit.open(&error) && !error.isEmpty(), "invalid capacity fails explicitly");
    std::cout << "Gateway offline queue tests passed.\n";
    return 0;
}
