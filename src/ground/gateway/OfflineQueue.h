#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace VaporView::Ground::Gateway
{

// Used only from the Gateway worker thread. Each item is an atomic disk record.
class OfflineQueue
{
public:
    struct Item
    {
        QString id;
        QJsonObject body;
        int priority = 0;
        qint64 createdMs = 0;
        qint64 expiresMs = 0;
    };

    explicit OfflineQueue(QString directory, qint64 maxBytes = 64 * 1024 * 1024);

    bool open(QString *error = nullptr);
    // Reusing an ID preserves the original item (idempotent enqueue).
    bool enqueue(const Item& item, QString *error = nullptr);
    QList<Item> peek(int maxItems = 64, qint64 maxPayloadBytes = 512 * 1024,
                     qint64 nowMs = QDateTime::currentMSecsSinceEpoch());
    bool remove(const QStringList& ids, QString *error = nullptr);
    qint64 bytes() const;
    int count() const;
    void prune(qint64 nowMs);

private:
    struct Entry
    {
        Item item;
        qint64 diskBytes = 0;
    };

    QString pathForId(const QString& id) const;
    bool removeAt(qsizetype index, QString *error);
    bool enforceLimit(QString *error);

    QString directory_;
    qint64 max_bytes_;
    qint64 bytes_ = 0;
    QList<Entry> entries_;
    bool opened_ = false;
};

}  // namespace VaporView::Ground::Gateway
