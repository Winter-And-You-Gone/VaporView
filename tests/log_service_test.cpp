#include "LogService.h"
#include "TelemetryCodec.h"
#include "logging/LogQueuePolicy.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

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

bool waitForFile(const QString& path, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) && !file.readAll().trimmed().isEmpty())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

bool waitForText(const QString& path, const QByteArray& text, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text) && file.readAll().contains(text))
        {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

QVector<QJsonObject> readJsonLines(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "open JSONL file for parsing");
    QVector<QJsonObject> records;
    while (!file.atEnd())
    {
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        require(document.isObject(), "every JSONL line is a complete object");
        records.push_back(document.object());
    }
    return records;
}

void silentQtMessageHandler(QtMsgType, const QMessageLogContext&, const QString&)
{
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary log directory");

    VaporView::LogRecord source;
    source.timestamp_utc = QStringLiteral("2026-07-30T12:00:00.000Z");
    source.timestamp_us = 123456;
    source.level = VaporView::LogLevel::Warning;
    source.source = QStringLiteral("Test");
    source.category = QStringLiteral("logging");
    source.sequence = 7;
    source.message = QStringLiteral("queue warning");
    source.fields.insert(QStringLiteral("dropped_count"), 3);

    const QByteArray payload = VaporView::TelemetryCodec::serializeLogRecord(source);
    VaporView::LogRecord parsed;
    require(VaporView::TelemetryCodec::parseLogRecord(payload, parsed), "log record IPC round trip");
    require(parsed.level == VaporView::LogLevel::Warning, "log level round trip");
    require(parsed.message == source.message, "log message round trip");
    require(parsed.fields.value(QStringLiteral("dropped_count")).toInt() == 3, "log fields round trip");

    // queueOverflowDropsLowPriorityFirst / criticalSurvivesQueueOverload
    using VaporView::LoggingInternal::QueueOverflowAction;
    const auto removeLow = VaporView::LoggingInternal::decideQueueOverflow(
        {VaporView::LogLevel::Warning,
         VaporView::LogLevel::Debug,
         VaporView::LogLevel::Error,
         VaporView::LogLevel::Info},
        VaporView::LogLevel::Warning);
    require(removeLow.action == QueueOverflowAction::RemoveLowPriorityAndEnqueue,
            "queue overflow removes low priority first");
    require(removeLow.low_priority_index == 1,
            "queue overflow removes the earliest low-priority record");
    require(removeLow.dropped_increment == 1,
            "removing a queued low-priority record increments the drop count");

    const QVector<VaporView::LogLevel> allHigh{
        VaporView::LogLevel::Warning,
        VaporView::LogLevel::Error,
        VaporView::LogLevel::Critical};
    const auto dropIncoming = VaporView::LoggingInternal::decideQueueOverflow(
        allHigh, VaporView::LogLevel::Info);
    require(dropIncoming.action == QueueOverflowAction::DropIncoming,
            "incoming low-priority record is dropped when the queue is all high priority");
    require(dropIncoming.dropped_increment == 1,
            "dropping the incoming record increments the drop count");
    const auto keepCritical = VaporView::LoggingInternal::decideQueueOverflow(
        allHigh, VaporView::LogLevel::Critical);
    require(keepCritical.action == QueueOverflowAction::EnqueueCriticalBeyondLimit,
            "critical record is never silently dropped");
    require(keepCritical.dropped_increment == 0,
            "keeping a critical record does not change the drop count");

    const VaporView::LogRecord dropNotice = VaporView::LoggingInternal::makeDropNotice(
        7, 91, 1234567, QStringLiteral("2026-07-31T00:00:00.000Z"), 12, 34);
    require(dropNotice.level == VaporView::LogLevel::Warning,
            "drop notice is warning level");
    require(dropNotice.sequence == 91,
            "drop notice uses its own assigned sequence");
    require(dropNotice.fields.value(QStringLiteral("dropped_count")).toULongLong() == 7,
            "drop notice preserves the aggregated drop count");

    {
        VaporView::LogService service(QStringLiteral("VaporViewTest"), &app, directory.path());
        const VaporView::LogRecord first = service.publish(VaporView::LogLevel::Info,
                                                            QStringLiteral("Test"),
                                                            QStringLiteral("sequence"),
                                                            QStringLiteral("first"));
        const VaporView::LogRecord second = service.publish(VaporView::LogLevel::Info,
                                                             QStringLiteral("Test"),
                                                             QStringLiteral("sequence"),
                                                             QStringLiteral("second"));
        require(second.sequence > first.sequence, "sequence is monotonic");
        service.publish(source);
        service.installQtMessageHandler();
        qWarning("qt handler test message");
        require(waitForFile(service.logFilePath()), "JSONL file is written");

        QFile file(service.logFilePath());
        require(file.open(QIODevice::ReadOnly | QIODevice::Text), "open JSONL file");
        bool foundSourceRecord = false;
        while (!file.atEnd())
        {
            const QJsonDocument document = QJsonDocument::fromJson(file.readLine());
            require(document.isObject(), "JSONL line is an object");
            const QJsonObject object = document.object();
            require(object.value(QStringLiteral("schema_version")).toInt() == 1,
                    "JSONL schema version");
            if (object.value(QStringLiteral("message")).toString() == source.message)
            {
                foundSourceRecord = true;
            }
        }
        require(foundSourceRecord, "JSONL preserves message");
        file.seek(0);
        require(QString::fromUtf8(file.readAll()).contains(QStringLiteral("qt handler test message")),
                "Qt message handler writes JSONL");

        service.publish(VaporView::LogLevel::Critical,
                        QStringLiteral("Test"),
                        QStringLiteral("critical"),
                        QStringLiteral("critical-sync"));
        require(waitForText(service.logFilePath(), QByteArrayLiteral("critical-sync"), 1000),
                "critical record is synchronously written");

#ifdef Q_OS_WIN
        QProcess process;
        VaporView::attachProcessLogging(&process,
                                        QStringLiteral("TestProcess"),
                                        QStringLiteral("child"));
        process.start(QStringLiteral("cmd.exe"),
                      {QStringLiteral("/d"),
                       QStringLiteral("/s"),
                       QStringLiteral("/c"),
                       QStringLiteral("echo process-stdout-1 & echo process-stdout-2 & echo process-stderr 1>&2")});
        require(process.waitForStarted(), "child process starts");
        require(process.waitForFinished(10000), "child process finishes");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        require(VaporView::processLoggedStandardOutput(&process).contains("process-stdout"),
                "captured stdout remains available to the process owner");
        require(VaporView::processLoggedStandardError(&process).contains("process-stderr"),
                "captured stderr remains available to the process owner");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stdout")),
                "child stdout is captured");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stdout-2")),
                "child stdout is split into lines");
        require(waitForText(service.logFilePath(), QByteArrayLiteral("process-stderr")),
                "child stderr is captured");

        QProcess largeProcess;
        VaporView::attachProcessLogging(&largeProcess,
                                        QStringLiteral("TestProcess"),
                                        QStringLiteral("child.large"));
        largeProcess.start(QStringLiteral("cmd.exe"),
                           {QStringLiteral("/d"),
                            QStringLiteral("/s"),
                            QStringLiteral("/c"),
                            QStringLiteral("for /L %i in (1,1,70000) do @echo 01234567890123456789")});
        require(largeProcess.waitForStarted(), "large-output child process starts");
        require(largeProcess.waitForFinished(15000), "large-output child process finishes");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        require(VaporView::processLoggedStandardOutput(&largeProcess).size() <= 1024 * 1024,
                "child stdout capture is bounded");
#endif
    }

    // idleInfoLogIsFlushedWithoutAnotherRecord
    {
        QTemporaryDir idleDirectory;
        require(idleDirectory.isValid(), "idle flush temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewIdleFlushTest"),
                                      &app,
                                      idleDirectory.path());
        require(waitForText(service.logFilePath(), QByteArrayLiteral("Application logging initialized.")),
                "initial lifecycle log is flushed");
        service.publish(VaporView::LogLevel::Info,
                        QStringLiteral("Test"),
                        QStringLiteral("idle.flush"),
                        QStringLiteral("idle-info-sentinel"));
        require(waitForText(service.logFilePath(), QByteArrayLiteral("idle-info-sentinel"), 2000),
                "idle Info log is flushed while LogService is still alive");
    }

    // criticalFlushPreservesQueuedOrder
    {
        QTemporaryDir fifoDirectory;
        require(fifoDirectory.isValid(), "critical FIFO temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewCriticalFifoTest"),
                                      &app,
                                      fifoDirectory.path());
        require(waitForText(service.logFilePath(), QByteArrayLiteral("Application logging initialized.")),
                "critical FIFO lifecycle log is flushed");
        QVector<quint64> acceptedSequences;
        for (int index = 0; index < 128; ++index)
        {
            acceptedSequences.push_back(service.publish(
                VaporView::LogLevel::Info,
                QStringLiteral("Test"),
                QStringLiteral("critical.fifo"),
                QStringLiteral("fifo-info-%1").arg(index)).sequence);
        }
        const VaporView::LogRecord critical = service.publish(
            VaporView::LogLevel::Critical,
            QStringLiteral("Test"),
            QStringLiteral("critical.fifo"),
            QStringLiteral("fifo-critical-sentinel"));

        const QVector<QJsonObject> records = readJsonLines(service.logFilePath());
        quint64 previousSequence = 0;
        int infoRecords = 0;
        bool foundCritical = false;
        for (const QJsonObject& object : records)
        {
            const quint64 sequence = object.value(QStringLiteral("sequence")).toVariant().toULongLong();
            require(sequence > previousSequence, "critical flush preserves monotonic file sequence order");
            previousSequence = sequence;
            const QString message = object.value(QStringLiteral("message")).toString();
            if (message.startsWith(QStringLiteral("fifo-info-")))
            {
                ++infoRecords;
            }
            if (message == QStringLiteral("fifo-critical-sentinel"))
            {
                foundCritical = true;
                require(sequence == critical.sequence, "critical JSON record preserves its sequence");
            }
        }
        require(infoRecords == acceptedSequences.size(),
                "all Info records accepted before Critical are persisted first");
        require(foundCritical, "Critical is persisted before publish returns");
    }

#ifdef Q_OS_WIN
    // fallbackAccessorsReportActualLogLocation
    {
        QTemporaryDir fallbackRoot;
        require(fallbackRoot.isValid(), "temporary fallback root");
        const bool hadLocalAppData = qEnvironmentVariableIsSet("LOCALAPPDATA");
        const QByteArray previousLocalAppData = qgetenv("LOCALAPPDATA");
        qputenv("LOCALAPPDATA", fallbackRoot.path().toUtf8());

        const QString blockedPath = QDir(fallbackRoot.path()).filePath(QStringLiteral("blocked"));
        QFile blocker(blockedPath);
        require(blocker.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "create blocked log path");
        blocker.close();
        const QString fallbackDirectory = QDir(fallbackRoot.path()).filePath(
            QStringLiteral("VaporView/logs"));
        {
            VaporView::LogService service(QStringLiteral("VaporViewFallbackTest"),
                                          &app,
                                          blockedPath + QStringLiteral("/logs"));
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("Test"),
                            QStringLiteral("fallback"),
                            QStringLiteral("fallback-accessor-sentinel"));
            require(QDir::cleanPath(service.logDirectory()) == QDir::cleanPath(fallbackDirectory),
                    "logDirectory reports the active fallback directory");
            require(QFileInfo(service.logFilePath()).absolutePath() ==
                        QFileInfo(fallbackDirectory).absoluteFilePath(),
                    "logFilePath reports a file inside the active fallback directory");
            require(QFileInfo::exists(service.logFilePath()),
                    "fallback path accessor points to the real JSONL file");
            require(waitForText(service.logFilePath(), QByteArrayLiteral("fallback-accessor-sentinel")),
                    "application log is persisted in the reported fallback path");
        }

        if (hadLocalAppData)
        {
            qputenv("LOCALAPPDATA", previousLocalAppData);
        }
        else
        {
            qunsetenv("LOCALAPPDATA");
        }
    }
#endif

    // fatalMessageIsPersistedBeforeAbort
    {
        QTemporaryDir fatalDirectory;
        require(fatalDirectory.isValid(), "Fatal helper temporary directory");
        QString helperPath = QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("log_fatal_helper"));
#ifdef Q_OS_WIN
        helperPath += QStringLiteral(".exe");
#endif
        require(QFileInfo::exists(helperPath), "Fatal helper executable exists");
        QProcess fatalProcess;
        fatalProcess.start(helperPath, {fatalDirectory.path()});
        require(fatalProcess.waitForStarted(5000), "Fatal helper starts");
        require(fatalProcess.waitForFinished(10000), "Fatal helper terminates within timeout");
        require(fatalProcess.exitStatus() == QProcess::CrashExit || fatalProcess.exitCode() != 0,
                "Fatal helper exits abnormally");

        const QFileInfoList fatalFiles = QDir(fatalDirectory.path()).entryInfoList(
            {QStringLiteral("VaporViewFatalTest-*.jsonl")}, QDir::Files, QDir::Name);
        require(fatalFiles.size() == 1, "Fatal helper creates one JSONL file");
        bool foundFatal = false;
        for (const QJsonObject& object : readJsonLines(fatalFiles.constFirst().absoluteFilePath()))
        {
            if (object.value(QStringLiteral("message")).toString() ==
                QStringLiteral("fatal-persistence-sentinel"))
            {
                foundFatal = true;
                require(object.value(QStringLiteral("level")).toString() == QStringLiteral("Critical"),
                        "Fatal message is persisted as a Critical JSON record");
            }
        }
        require(foundFatal, "Fatal JSON record is persisted before abort");
    }

    // concurrentPublishProducesValidJsonLines
    {
        QTemporaryDir concurrentDirectory;
        require(concurrentDirectory.isValid(), "concurrent publish temporary directory");
        VaporView::LogService service(QStringLiteral("VaporViewConcurrentTest"),
                                      &app,
                                      concurrentDirectory.path());
        constexpr int kThreadCount = 4;
        constexpr int kRecordsPerThread = 500;
        std::vector<std::thread> workers;
        workers.reserve(kThreadCount);
        for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
        {
            workers.emplace_back([&service, threadIndex]() {
                for (int recordIndex = 0; recordIndex < kRecordsPerThread; ++recordIndex)
                {
                    service.publish(VaporView::LogLevel::Info,
                                    QStringLiteral("ConcurrentTest"),
                                    QStringLiteral("concurrent.publish"),
                                    QStringLiteral("thread-%1-record-%2")
                                        .arg(threadIndex)
                                        .arg(recordIndex));
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        service.publish(VaporView::LogLevel::Critical,
                        QStringLiteral("ConcurrentTest"),
                        QStringLiteral("concurrent.publish"),
                        QStringLiteral("concurrent-publish-sentinel"));
        int concurrentRecords = 0;
        for (const QJsonObject& object : readJsonLines(service.logFilePath()))
        {
            if (object.value(QStringLiteral("category")).toString() ==
                QStringLiteral("concurrent.publish") &&
                object.value(QStringLiteral("message")).toString() !=
                    QStringLiteral("concurrent-publish-sentinel"))
            {
                ++concurrentRecords;
            }
        }
        require(concurrentRecords == kThreadCount * kRecordsPerThread,
                "concurrent publish produces complete parseable JSONL records");
    }

    // concurrentQtLoggingDuringShutdownIsSafe
    {
        QtMessageHandler originalHandler = qInstallMessageHandler(&silentQtMessageHandler);
        for (int round = 0; round < 6; ++round)
        {
            QTemporaryDir shutdownDirectory;
            require(shutdownDirectory.isValid(), "concurrent shutdown temporary directory");
            auto service = std::make_unique<VaporView::LogService>(
                QStringLiteral("VaporViewShutdownTest%1").arg(round),
                nullptr,
                shutdownDirectory.path());
            service->installQtMessageHandler();

            std::atomic_bool start{false};
            std::atomic_bool stop{false};
            std::atomic_int ready{0};
            std::atomic_int published{0};
            std::vector<std::thread> workers;
            for (int index = 0; index < 4; ++index)
            {
                workers.emplace_back([&]() {
                    ready.fetch_add(1, std::memory_order_release);
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    while (!stop.load(std::memory_order_acquire))
                    {
                        qInfo("shutdown-race-message");
                        published.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            while (ready.load(std::memory_order_acquire) != 4)
            {
                std::this_thread::yield();
            }
            start.store(true, std::memory_order_release);
            QElapsedTimer overlapTimer;
            overlapTimer.start();
            while (published.load(std::memory_order_acquire) < 100 && overlapTimer.elapsed() < 2000)
            {
                QThread::msleep(1);
            }
            require(published.load(std::memory_order_acquire) >= 100,
                    "Qt logging overlaps controlled shutdown");
            service.reset();
            stop.store(true, std::memory_order_release);
            for (std::thread& worker : workers)
            {
                worker.join();
            }
        }
        qInstallMessageHandler(originalHandler);
    }

    // dropNoticeIsPersistedOnShutdown
    {
        QTemporaryDir overloadDirectory;
        require(overloadDirectory.isValid(), "queue overload temporary directory");
        QString overloadPath;
        quint64 lastPublishedSequence = 0;
        {
            VaporView::LogService service(QStringLiteral("VaporViewOverloadTest"),
                                          &app,
                                          overloadDirectory.path());
            for (int index = 0; index < 50000; ++index)
            {
                lastPublishedSequence = service.publish(
                    VaporView::LogLevel::Debug,
                    QStringLiteral("OverloadTest"),
                    QStringLiteral("queue.overload"),
                    QStringLiteral("overload-record-%1").arg(index)).sequence;
            }
            overloadPath = service.logFilePath();
        }
        bool foundDropNotice = false;
        bool foundShutdownDropNotice = false;
        for (const QJsonObject& object : readJsonLines(overloadPath))
        {
            if (object.value(QStringLiteral("source")).toString() == QStringLiteral("LogService") &&
                object.value(QStringLiteral("category")).toString() == QStringLiteral("queue"))
            {
                foundDropNotice = true;
                require(object.value(QStringLiteral("level")).toString() == QStringLiteral("Warning"),
                        "shutdown drop notice is Warning");
                require(object.value(QStringLiteral("fields")).toObject()
                            .value(QStringLiteral("dropped_count")).toDouble() > 0,
                        "shutdown drop notice reports a positive dropped count");
                if (object.value(QStringLiteral("sequence")).toVariant().toULongLong() >
                    lastPublishedSequence)
                {
                    foundShutdownDropNotice = true;
                }
            }
        }
        require(foundDropNotice, "queue overload produces an aggregated drop notice");
        require(foundShutdownDropNotice,
                "remaining queue drop notice is persisted with a new sequence during shutdown");
    }

    // retentionLimitIncludesNewActiveFile
    {
        QTemporaryDir retentionDirectory;
        require(retentionDirectory.isValid(), "retention temporary directory");
        const QDateTime baseTime = QDateTime::currentDateTimeUtc().addDays(-20);
        QString oldestPath;
        QString newestPath;
        for (int index = 0; index < 10; ++index)
        {
            const QString path = QDir(retentionDirectory.path()).filePath(
                QStringLiteral("VaporViewRetentionTest-2026-06-%1.jsonl")
                    .arg(index + 1, 2, 10, QLatin1Char('0')));
            QFile file(path);
            require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                    "create pre-existing retention log");
            file.write("{\"old\":true}\n");
            require(file.setFileTime(baseTime.addSecs(index), QFileDevice::FileModificationTime),
                    "set deterministic retention modification time");
            file.close();
            if (index == 0) oldestPath = path;
            if (index == 9) newestPath = path;
        }

        QString activePath;
        {
            VaporView::LogService service(QStringLiteral("VaporViewRetentionTest"),
                                          &app,
                                          retentionDirectory.path());
            service.publish(VaporView::LogLevel::Critical,
                            QStringLiteral("Test"),
                            QStringLiteral("retention"),
                            QStringLiteral("retention-active-sentinel"));
            activePath = service.logFilePath();
            require(QFileInfo::exists(activePath), "retention keeps the active file");
        }
        const QFileInfoList retained = QDir(retentionDirectory.path()).entryInfoList(
            {QStringLiteral("VaporViewRetentionTest-*.jsonl"),
             QStringLiteral("VaporViewRetentionTest-*.jsonl.*")},
            QDir::Files,
            QDir::Name);
        require(retained.size() <= 10, "retention limit includes the newly created active file");
        require(!QFileInfo::exists(oldestPath), "retention removes the oldest pre-existing file");
        require(QFileInfo::exists(newestPath), "retention preserves the newest pre-existing file");
        require(QFileInfo::exists(activePath), "retention never deletes the active file");
        qint64 totalBytes = 0;
        for (const QFileInfo& fileInfo : retained)
        {
            totalBytes += fileInfo.size();
        }
        require(totalBytes <= 100LL * 1024LL * 1024LL,
                "retention keeps total bytes within the configured limit");
    }

    QTemporaryDir rotationDirectory;
    require(rotationDirectory.isValid(), "temporary rotation directory");
    {
        VaporView::LogService service(QStringLiteral("VaporViewRotationTest"),
                                      &app,
                                      rotationDirectory.path());
        const QString largeMessage(1100 * 1024, QLatin1Char('x'));
        for (int index = 0; index < 100; ++index)
        {
            service.publish(VaporView::LogLevel::Debug,
                            QStringLiteral("Test"),
                            QStringLiteral("rotation"),
                            largeMessage);
        }
        require(waitForFile(service.logFilePath() + QStringLiteral(".1"), 15000),
                "JSONL rotates before exceeding 10 MiB");
    }

    const QDir rotationDirectoryView(rotationDirectory.path());
    const QFileInfoList retainedLogs = rotationDirectoryView.entryInfoList(
        {QStringLiteral("VaporViewRotationTest-*.jsonl"),
         QStringLiteral("VaporViewRotationTest-*.jsonl.*")},
        QDir::Files,
        QDir::Time);
    require(retainedLogs.size() <= 10, "rotation retains at most ten files");
    qint64 retainedBytes = 0;
    for (const QFileInfo& fileInfo : retainedLogs)
    {
        retainedBytes += fileInfo.size();
    }
    require(retainedBytes <= 100LL * 1024LL * 1024LL,
            "rotation keeps total log size within the configured limit");

    return 0;
}
