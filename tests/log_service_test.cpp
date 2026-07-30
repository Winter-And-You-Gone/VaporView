#include "LogService.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
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

bool waitForFile(const QString& path)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
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
    }

    return 0;
}
