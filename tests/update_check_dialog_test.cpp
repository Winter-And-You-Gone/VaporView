#include "ground/main/MainWindow.h"
#include "test_ui_helpers.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QHostAddress>
#include <QLabel>
#include <QElapsedTimer>
#include <QNetworkProxy>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <memory>

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

void serveManifest(QTcpServer& server, QByteArray manifest, bool& requestServed)
{
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, manifest, &requestServed]() {
        while (server.hasPendingConnections())
        {
            QTcpSocket *socket = server.nextPendingConnection();
            socket->setParent(&server);
            auto requestBuffer = std::make_shared<QByteArray>();
            const auto handleRequest = [socket, requestBuffer, manifest, &requestServed]() {
                if (socket->property("vaporViewManifestResponseSent").toBool())
                {
                    return;
                }
                requestBuffer->append(socket->readAll());
                if (!requestBuffer->contains("\r\n\r\n"))
                {
                    return;
                }

                requestServed = requestBuffer->contains("GET /repository/Updates.xml ");
                const QByteArray response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/xml\r\n"
                    "Content-Length: " + QByteArray::number(manifest.size()) + "\r\n"
                    "Connection: close\r\n"
                    "\r\n" +
                    manifest;
                socket->write(response);
                socket->setProperty("vaporViewManifestResponseSent", true);
                socket->flush();
                QTimer::singleShot(1000, socket, &QTcpSocket::disconnectFromHost);
            };
            QObject::connect(socket, &QTcpSocket::readyRead, socket, handleRequest);
            handleRequest();
        }
    });
}

}  // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VaporView"));
    app.setApplicationVersion(QStringLiteral("1.0.5"));
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0),
            "local update manifest server starts");

    bool requestServed = false;
    serveManifest(server,
                  QByteArrayLiteral("<Updates><PackageUpdate><Name>com.vaporview.core</Name><Version>9.9.9</Version></PackageUpdate></Updates>"),
                  requestServed);

    const QByteArray repositoryUrl =
        QByteArrayLiteral("http://127.0.0.1:") +
        QByteArray::number(server.serverPort()) +
        QByteArrayLiteral("/repository/");
    qputenv("VAPORVIEW_IFW_REPOSITORY_URL", repositoryUrl);

    MainWindow window;
    window.resize(1180, 760);
    window.show();
    require(VaporViewTest::waitForWindowExposed(&window),
            "main window is exposed before invoking update check");

    QAction *checkUpdatesAction = window.findChild<QAction *>(QStringLiteral("checkUpdatesAction"));
    require(checkUpdatesAction != nullptr, "check updates action exists");

    bool sawDialog = false;
    bool sawAvailable = false;
    bool sawDevelopmentBuildDetail = false;
    bool updateButtonHidden = false;
    QString lastStatusText;
    QString lastDetailText;
    bool lastUpdateButtonVisible = false;
    QElapsedTimer elapsed;
    elapsed.start();

    QTimer inspector;
    QObject::connect(&inspector, &QTimer::timeout, &window, [&]() {
        QDialog *dialog = window.findChild<QDialog *>(QStringLiteral("updateCheckDialog"));
        if (!dialog)
        {
            return;
        }

        sawDialog = true;
        auto *statusLabel = dialog->findChild<QLabel *>(QStringLiteral("updateCheckStatusLabel"));
        auto *detailLabel = dialog->findChild<QLabel *>(QStringLiteral("updateCheckDetailLabel"));
        auto *updateButton = dialog->findChild<QPushButton *>(QStringLiteral("updateCheckUpdateButton"));
        lastStatusText = statusLabel ? statusLabel->text() : QStringLiteral("<missing status>");
        lastDetailText = detailLabel ? detailLabel->text() : QStringLiteral("<missing detail>");
        lastUpdateButtonVisible = updateButton && updateButton->isVisible();

        sawAvailable = statusLabel &&
            (statusLabel->text().contains(QStringLiteral("发现可用更新")) ||
             statusLabel->text().contains(QStringLiteral("Updates are available")));
        sawDevelopmentBuildDetail = detailLabel &&
            detailLabel->text().contains(QStringLiteral("9.9.9")) &&
            (detailLabel->text().contains(QStringLiteral("开发构建")) ||
             detailLabel->text().contains(QStringLiteral("development build"), Qt::CaseInsensitive));
        updateButtonHidden = updateButton && !updateButton->isVisible();

        if (sawAvailable && sawDevelopmentBuildDetail && updateButtonHidden)
        {
            dialog->accept();
        }
        else if (elapsed.elapsed() > 4500)
        {
            dialog->reject();
        }
    });
    inspector.start(20);

    QTimer::singleShot(0, checkUpdatesAction, &QAction::trigger);
    const bool matchedExpectedState = VaporViewTest::processEventsUntil(5000, [&]() {
        return sawAvailable && sawDevelopmentBuildDetail && updateButtonHidden;
    });
    if (!matchedExpectedState)
    {
        std::cerr << "FAIL: development build update check did not report expected state\n"
                  << "sawDialog=" << sawDialog
                  << " sawAvailable=" << sawAvailable
                  << " sawDevelopmentBuildDetail=" << sawDevelopmentBuildDetail
                  << " updateButtonHidden=" << updateButtonHidden
                  << " requestServed=" << requestServed
                  << " lastUpdateButtonVisible=" << lastUpdateButtonVisible
                  << "\nstatus=" << lastStatusText.toLocal8Bit().constData()
                  << "\ndetail=" << lastDetailText.toLocal8Bit().constData()
                  << "\n";
        return 1;
    }

    inspector.stop();
    require(sawDialog, "update dialog opened");
    require(requestServed, "update check requested repository Updates.xml");

    window.close();
    VaporViewTest::processEventsFor(50);
    std::cout << "update_check_dialog_test passed\n";
    return 0;
}
