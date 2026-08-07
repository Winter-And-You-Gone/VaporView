#include "app/LifecycleBreadcrumb.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{

void fail(const QString& message)
{
    std::cerr << message.toLocal8Bit().constData() << '\n';
    std::exit(1);
}

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        fail(message);
    }
}

std::filesystem::path toPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString fromPath(const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::vector<QJsonObject> readJsonLines(const std::filesystem::path& path)
{
    QFile file(fromPath(path));
    require(file.open(QIODevice::ReadOnly | QIODevice::Text),
            QStringLiteral("failed to open breadcrumb file"));

    std::vector<QJsonObject> objects;
    while (!file.atEnd())
    {
        const QByteArray line = file.readLine().trimmed();
        require(!line.contains('\n'), QStringLiteral("breadcrumb record spans multiple lines"));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        require(error.error == QJsonParseError::NoError,
                QStringLiteral("breadcrumb line is not valid JSON: %1").arg(error.errorString()));
        require(document.isObject(), QStringLiteral("breadcrumb line is not a JSON object"));
        objects.push_back(document.object());
    }
    return objects;
}

void testNormalWrite()
{
    QTemporaryDir directory;
    require(directory.isValid(), QStringLiteral("temporary directory is valid"));

    const std::filesystem::path path = toPath(directory.path());
    require(VaporView::LifecycleBreadcrumbTest::writeLifecycleBreadcrumbToDirectory(
                path, "process_entry"),
            QStringLiteral("process_entry breadcrumb write succeeds"));
    require(VaporView::LifecycleBreadcrumbTest::writeLifecycleBreadcrumbToDirectory(
                path, "app_exec_returned", 1, "quoted_\"reason\\path"),
            QStringLiteral("app_exec_returned breadcrumb write succeeds"));

    const std::vector<QJsonObject> objects = readJsonLines(
        VaporView::LifecycleBreadcrumbTest::lifecycleBreadcrumbFilePath(path));
    require(objects.size() == 2, QStringLiteral("breadcrumb file has two JSONL records"));

    const QJsonObject first = objects.at(0);
    require(first.value(QStringLiteral("event")).toString() == QStringLiteral("process_entry"),
            QStringLiteral("first breadcrumb event is process_entry"));
    require(!first.value(QStringLiteral("timestamp_utc")).toString().isEmpty(),
            QStringLiteral("timestamp_utc is populated"));
    require(first.value(QStringLiteral("timestamp_utc")).toString().endsWith(QLatin1Char('Z')),
            QStringLiteral("timestamp_utc uses UTC Z suffix"));
    require(first.value(QStringLiteral("process_id")).toInteger() > 0,
            QStringLiteral("process_id is populated"));
    require(first.value(QStringLiteral("thread_id")).toInteger() > 0,
            QStringLiteral("thread_id is populated"));
    require(first.value(QStringLiteral("sequence")).toInteger() > 0,
            QStringLiteral("sequence is populated"));

    const QJsonObject second = objects.at(1);
    require(second.value(QStringLiteral("event")).toString() == QStringLiteral("app_exec_returned"),
            QStringLiteral("second breadcrumb event is app_exec_returned"));
    require(second.value(QStringLiteral("exit_code")).toInt() == 1,
            QStringLiteral("exit_code is preserved"));
    require(second.value(QStringLiteral("reason_code")).toString() ==
                QStringLiteral("quoted_\"reason\\path"),
            QStringLiteral("reason_code is JSON-escaped and restored"));
}

void testWriteFailureDoesNotThrow()
{
    QTemporaryDir directory;
    require(directory.isValid(), QStringLiteral("temporary directory is valid"));

    QTemporaryFile blocker(directory.filePath(QStringLiteral("not-a-directory")));
    require(blocker.open(), QStringLiteral("blocker file is created"));

    const bool written = VaporView::LifecycleBreadcrumbTest::writeLifecycleBreadcrumbToDirectory(
        toPath(blocker.fileName()), "process_entry");
    require(!written, QStringLiteral("write to non-directory path fails cleanly"));
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testNormalWrite();
    testWriteFailureDoesNotThrow();

    std::cout << "lifecycle_breadcrumb_test passed\n";
    return 0;
}