#include "LogService.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

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

#ifdef Q_OS_WIN
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
        const QString fallbackFile = QDir(fallbackRoot.path()).filePath(
            QStringLiteral("VaporView/logs/VaporViewFallbackTest-%1.jsonl")
                .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
        {
            VaporView::LogService service(QStringLiteral("VaporViewFallbackTest"),
                                          &app,
                                          blockedPath + QStringLiteral("/logs"));
            require(waitForFile(fallbackFile, 5000),
                    "application log falls back after the primary path fails");
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
