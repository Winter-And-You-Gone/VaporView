#include "map3d/MapResourceManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QDebug>

namespace {

int fail(const QString& message)
{
    qCritical().noquote() << message;
    return 1;
}

bool waitForOperation(VaporView::Map3D::MapResourceManager& manager,
                      const QString& packageId,
                      bool* success,
                      QString* message)
{
    bool completed = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    const QMetaObject::Connection operationConnection = QObject::connect(
        &manager,
        &VaporView::Map3D::MapResourceManager::operationFinished,
        &loop,
        [&](const QString& id, bool operationSuccess, const QString& operationMessage) {
            if (id != packageId)
            {
                return;
            }
            completed = true;
            if (success)
            {
                *success = operationSuccess;
            }
            if (message)
            {
                *message = operationMessage;
            }
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    QObject::disconnect(operationConnection);
    return completed;
}

bool refreshManifestAndWait(VaporView::Map3D::MapResourceManager& manager,
                            bool* success,
                            QString* message)
{
    bool completed = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    const QMetaObject::Connection operationConnection = QObject::connect(
        &manager,
        &VaporView::Map3D::MapResourceManager::operationFinished,
        &loop,
        [&](const QString& id, bool operationSuccess, const QString& operationMessage) {
            if (!id.isEmpty())
            {
                return;
            }
            completed = true;
            if (success)
            {
                *success = operationSuccess;
            }
            if (message)
            {
                *message = operationMessage;
            }
            loop.quit();
        });
    manager.refreshManifest();
    if (!completed)
    {
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000);
        loop.exec();
    }
    QObject::disconnect(operationConnection);
    return completed;
}

bool removePackageAndWait(VaporView::Map3D::MapResourceManager& manager,
                          const QString& packageId,
                          bool* success,
                          QString* message)
{
    bool completed = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const QMetaObject::Connection operationConnection = QObject::connect(
        &manager,
        &VaporView::Map3D::MapResourceManager::operationFinished,
        &loop,
        [&](const QString& id, bool operationSuccess, const QString& operationMessage) {
            if (id != packageId)
            {
                return;
            }
            completed = true;
            if (success)
            {
                *success = operationSuccess;
            }
            if (message)
            {
                *message = operationMessage;
            }
            loop.quit();
        });
    manager.removePackage(packageId);
    if (!completed)
    {
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000);
        loop.exec();
    }
    QObject::disconnect(operationConnection);
    return completed;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QTcpServer server;
    if (!server.listen(QHostAddress::AnyIPv4))
    {
        return fail(QStringLiteral("could not start local HTTP server: %1").arg(server.errorString()));
    }
    const QString uniqueDirectory = QStringLiteral(".vaporview-map-resource-test-%1-%2")
                                        .arg(QCoreApplication::applicationPid())
                                        .arg(QRandomGenerator::global()->generate());
    const QString relativePath = uniqueDirectory + QStringLiteral("/payload.bin");
    const QString targetPath = QDir(VaporView::Map3D::MapResourceManager::defaultDownloadRoot())
                                   .absoluteFilePath(relativePath);
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    QFile::remove(targetPath);

    const QByteArray payload("VaporView map resource test payload\n");
    QByteArray servedPayload = payload;
    const QByteArray sha256 = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    const QString packageId = QStringLiteral("http-test-%1").arg(QCoreApplication::applicationPid());

    QJsonObject fileObject;
    fileObject.insert(QStringLiteral("relativePath"), relativePath);
    fileObject.insert(QStringLiteral("url"), QStringLiteral("payload.bin"));
    fileObject.insert(QStringLiteral("sizeBytes"), payload.size());
    fileObject.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256));

    QJsonObject packageObject;
    packageObject.insert(QStringLiteral("id"), packageId);
    packageObject.insert(QStringLiteral("displayName"), QStringLiteral("HTTP test map"));
    packageObject.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    packageObject.insert(QStringLiteral("sizeBytes"), payload.size());
    packageObject.insert(QStringLiteral("requiredFiles"), QJsonArray{relativePath});
    packageObject.insert(QStringLiteral("files"), QJsonArray{fileObject});
    const QByteArray manifest = QJsonDocument(QJsonObject{
                                                  {QStringLiteral("schema"), QStringLiteral("vaporview.map-resources")},
                                                  {QStringLiteral("schemaVersion"), 1},
                                                  {QStringLiteral("resources"), QJsonArray{packageObject}}
                                              })
                                    .toJson(QJsonDocument::Compact);

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        while (server.hasPendingConnections())
        {
            QTcpSocket* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            const auto handleRequest = [socket, &manifest, &servedPayload]() {
                if (socket->property("vaporviewResponded").toBool())
                {
                    return;
                }
                QByteArray request = socket->property("vaporviewRequest").toByteArray();
                request += socket->readAll();
                socket->setProperty("vaporviewRequest", request);
                if (!request.contains("\r\n\r\n"))
                {
                    return;
                }

                const int firstSpace = request.indexOf(' ');
                const int secondSpace = firstSpace < 0 ? -1 : request.indexOf(' ', firstSpace + 1);
                const QByteArray path = (firstSpace >= 0 && secondSpace > firstSpace)
                                            ? request.mid(firstSpace + 1, secondSpace - firstSpace - 1)
                                            : QByteArray();
                const bool isManifest = path == "/manifest.json";
                const bool isPayload = path == "/payload.bin";
                const QByteArray body = isManifest ? manifest : (isPayload ? servedPayload : QByteArray());
                const QByteArray status = (isManifest || isPayload) ? QByteArray("200 OK")
                                                                      : QByteArray("404 Not Found");
                const QByteArray response = QByteArray("HTTP/1.1 ") + status +
                                            "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                            "\r\nConnection: close\r\nContent-Type: application/octet-stream\r\n\r\n" + body;
                socket->setProperty("vaporviewResponded", true);
                socket->write(response);
                socket->disconnectFromHost();
            };
            QObject::connect(socket, &QTcpSocket::readyRead, socket, handleRequest);
            if (socket->bytesAvailable() > 0)
            {
                handleRequest();
            }
        }
    });

    VaporView::Map3D::MapResourceManager manager;
    bool operationSuccess = false;
    QString operationMessage;
    manager.setManifestUrl(QStringLiteral("http://example.test/manifest.json"));
    if (!refreshManifestAndWait(manager, &operationSuccess, &operationMessage) || operationSuccess)
    {
        return fail(QStringLiteral("remote HTTP manifest was not rejected: %1").arg(operationMessage));
    }

    const QString manifestUrl = QStringLiteral("http://127.0.0.1:%1/manifest.json").arg(server.serverPort());
    manager.setManifestUrl(manifestUrl);

    if (!refreshManifestAndWait(manager, &operationSuccess, &operationMessage) || !operationSuccess)
    {
        return fail(QStringLiteral("manifest download failed: %1").arg(operationMessage));
    }
    if (manager.packages().size() != 1 || manager.packages().first().id != packageId)
    {
        return fail(QStringLiteral("local HTTP manifest did not produce the expected package"));
    }

    const VaporView::Map3D::MapResourcePackage package = manager.packages().first();
    manager.downloadPackage(packageId);
    if (!waitForOperation(manager, packageId, &operationSuccess, &operationMessage) || !operationSuccess)
    {
        return fail(QStringLiteral("map resource download failed: %1").arg(operationMessage));
    }
    if (!QFileInfo(targetPath).isFile())
    {
        return fail(QStringLiteral("downloaded map resource was not written"));
    }

    QString reason;
    if (!manager.packageInstalled(package, &reason))
    {
        return fail(QStringLiteral("downloaded map resource did not pass verification: %1").arg(reason));
    }

    servedPayload[0] = servedPayload[0] == 'X' ? 'Y' : 'X';
    manager.downloadPackage(packageId);
    if (!waitForOperation(manager, packageId, &operationSuccess, &operationMessage) || operationSuccess)
    {
        return fail(QStringLiteral("checksum mismatch was not rejected: %1").arg(operationMessage));
    }
    QFile restored(targetPath);
    const bool restoredOpen = restored.open(QIODevice::ReadOnly);
    const QByteArray restoredPayload = restoredOpen ? restored.readAll() : QByteArray();
    if (!restoredOpen || restoredPayload != payload)
    {
        return fail(QStringLiteral("checksum failure did not restore the previous map resource"));
    }
    restored.close();

    if (!removePackageAndWait(manager, packageId, &operationSuccess, &operationMessage) || !operationSuccess)
    {
        return fail(QStringLiteral("map resource removal failed: %1").arg(operationMessage));
    }
    const bool removed = !QFileInfo::exists(targetPath);
    QDir(QFileInfo(targetPath).absolutePath()).removeRecursively();
    return removed ? 0 : fail(QStringLiteral("map resource file remained after removal"));
}
