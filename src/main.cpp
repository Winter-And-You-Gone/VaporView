#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include "QmlAppController.h"
#include "RtkController.h"
#include "SessionController.h"

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
#ifdef WIN32
    qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("VaporView");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList iconCandidates = {
        QDir(appDir).filePath("resources/app.ico"),
        QDir(appDir).filePath("../resources/app.ico"),
        QDir(appDir).filePath("../../resources/app.ico")
    };

    QIcon windowIcon;
    for (const QString& path : iconCandidates)
    {
        if (QFileInfo::exists(path))
        {
            windowIcon = QIcon(path);
            if (!windowIcon.isNull())
            {
                break;
            }
        }
    }
    if (!windowIcon.isNull())
    {
        app.setWindowIcon(windowIcon);
    }

    QmlAppController controller;
    RtkController rtkController;
    SessionController sessionController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("rtkController", &rtkController);
    engine.rootContext()->setContextProperty("sessionController", &sessionController);
    engine.addImportPath(appDir);
    engine.addImportPath(QDir(appDir).filePath("qml"));
    const QString qmlLogPath = QDir(appDir).filePath("qml_boot.log");

    QObject::connect(&engine, &QQmlEngine::warnings, &app, [qmlLogPath](const QList<QQmlError> &warnings) {
        QFile file(qmlLogPath);
        if (!file.open(QIODevice::Append | QIODevice::Text))
        {
            return;
        }

        QTextStream stream(&file);
        for (const QQmlError &warning : warnings)
        {
            stream << warning.toString() << '\n';
        }
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [qmlLogPath]() {
            QFile file(qmlLogPath);
            if (file.open(QIODevice::Append | QIODevice::Text))
            {
                QTextStream stream(&file);
                stream << "objectCreationFailed" << '\n';
            }
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    QObject::connect(&controller, &QmlAppController::englishChanged, &app, [&controller, &rtkController, &sessionController]() {
        rtkController.setEnglish(controller.english());
        sessionController.setEnglish(controller.english());
    });

    engine.loadFromModule("VaporViewApp", "App");

    return app.exec();
}

