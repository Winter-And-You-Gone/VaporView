#include "shared/concurrency/BoundedByteQueue.h"

#include <QByteArray>

#include <cstdlib>
#include <iostream>

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
}

int main()
{
    using Queue = VaporView::BoundedByteQueue<QByteArray>;
    Queue queue(5);
    queue.reset(true);

    require(queue.push(QByteArrayLiteral("abc"), 3).status == Queue::PushStatus::Enqueued,
            "first record fits");
    const auto full = queue.push(QByteArrayLiteral("def"), 3);
    require(full.status == Queue::PushStatus::Full && full.queuedBytes == 3,
            "queue rejects a record that would exceed the byte limit");
    require(full.droppedRecords == 1, "full queue increments the dropped-record count");

    QByteArray record;
    require(queue.waitPop(&record) && record == QByteArrayLiteral("abc"),
            "queue returns the enqueued record");
    queue.close();
    require(!queue.waitPop(&record), "closed empty queue stops the consumer");
    require(queue.push(QByteArrayLiteral("x"), 1).status == Queue::PushStatus::Closed,
            "closed queue rejects new records");

    Queue recordLimitedQueue(100, 2);
    recordLimitedQueue.reset(true);
    require(recordLimitedQueue.push(QByteArrayLiteral("a"), 1).status == Queue::PushStatus::Enqueued &&
                recordLimitedQueue.push(QByteArrayLiteral("b"), 1).status == Queue::PushStatus::Enqueued,
            "record-limited queue accepts records up to its count limit");
    require(recordLimitedQueue.push(QByteArrayLiteral("c"), 1).status == Queue::PushStatus::Full,
            "record count limit prevents many tiny records from accumulating");

    std::cout << "bounded_byte_queue_test passed\n";
    return 0;
}
